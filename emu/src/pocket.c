/*
 * Mecrisp Pocket - the virtual board.
 * Copyright (C) 2026 The Mecrisp Pocket authors. GPL-3.0-or-later.
 *
 * All guest state (CPU, memory, device registers) is touched only by the
 * thread inside qp_run(). Other threads talk to it through lock-free UART
 * rings, atomic request flags, and run_on_vm() for everything else.
 */
#define _POSIX_C_SOURCE 200809L
#include "qp/pocket.h"
#include "rv32.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BATCH        4096u
#define RING_SIZE    65536u          /* power of two */
#define DEV_SLOT_FIRST 0x7u          /* vibrator */
#define DEV_SLOT_LAST  0xCu          /* GPIO */
#define DEV_WORDS    64u             /* registers per generic device slot */
#define MAX_WAIT_MS  100

struct ring {
    uint8_t buf[RING_SIZE];
    _Atomic uint32_t head;  /* producer index */
    _Atomic uint32_t tail;  /* consumer index */
};

static int ring_push(struct ring *r, uint8_t b)
{
    uint32_t h = atomic_load_explicit(&r->head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&r->tail, memory_order_acquire);
    if (h - t >= RING_SIZE) return 0;
    r->buf[h & (RING_SIZE - 1)] = b;
    atomic_store_explicit(&r->head, h + 1, memory_order_release);
    return 1;
}

static int ring_pop(struct ring *r, uint8_t *b)
{
    uint32_t t = atomic_load_explicit(&r->tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&r->head, memory_order_acquire);
    if (h == t) return 0;
    *b = r->buf[t & (RING_SIZE - 1)];
    atomic_store_explicit(&r->tail, t + 1, memory_order_release);
    return 1;
}

static uint32_t ring_count(struct ring *r)
{
    return atomic_load_explicit(&r->head, memory_order_acquire)
         - atomic_load_explicit(&r->tail, memory_order_acquire);
}

/* Consumer side only. */
static void ring_drop_all(struct ring *r)
{
    atomic_store_explicit(&r->tail, atomic_load_explicit(&r->head, memory_order_acquire),
                          memory_order_release);
}

struct vm_request {
    void (*fn)(qp_vm *vm, void *arg);
    void *arg;
};

struct qp_vm {
    qp_config cfg;

    pthread_mutex_t lock;
    pthread_cond_t vm_cv;     /* wakes the VM thread */
    pthread_cond_t host_cv;   /* wakes host threads: idle, output, request done */

    /* Guest state (VM thread only while running). */
    struct rv_cpu cpu;
    struct rv_bus bus;
    uint8_t flash[QP_FLASH_SIZE];
    uint8_t ram[QP_RAM_SIZE];
    uint8_t flash_dirty[QP_FLASH_SIZE / 4096];
    uint32_t irq_raw, irq_enable;
    uint64_t mtimecmp;
    uint32_t reset_cause, safe_boot;
    uint32_t flash_addr;
    uint32_t protected_writes;
    uint32_t devregs[DEV_SLOT_LAST - DEV_SLOT_FIRST + 1][DEV_WORDS];

    struct ring rx, tx;
    _Atomic int tx_new;

    /* Time */
    uint64_t start_ns;
    uint64_t paused_total_ns;
    uint64_t paused_since_ns;
    uint64_t virtual_offset_us;

    /* Control (protected by lock unless atomic) */
    int running;
    int shutdown;
    int paused;
    int step_req;
    int in_wfi;
    _Atomic int break_req;
    _Atomic int reset_req;        /* 0 none, else cause + 1 */
    _Atomic int reset_safe;
    _Atomic uint32_t raise_bits;

    struct vm_request *request;   /* single pending request slot */
    uint64_t request_seq, request_done_seq;
};

/* ---------------------------------------------------------------------- */
/* Time                                                                   */
/* ---------------------------------------------------------------------- */

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t run_ns(qp_vm *vm)
{
    uint64_t t = now_ns();
    uint64_t paused = vm->paused_total_ns;
    if (vm->paused_since_ns) paused += t - vm->paused_since_ns;
    return t - vm->start_ns - paused;
}

static uint64_t mtime_us(qp_vm *vm)
{
    if (vm->cfg.virtual_time)
        return vm->cpu.instret / 100 + vm->virtual_offset_us;
    return run_ns(vm) / 1000;
}

static uint64_t read_time_cb(void *ctx) { return mtime_us((qp_vm *)ctx); }

static uint64_t read_cycle_cb(void *ctx)
{
    qp_vm *vm = ctx;
    if (vm->cfg.virtual_time)
        return vm->cpu.instret + vm->virtual_offset_us * 100;
    return run_ns(vm) / 10;  /* nominal 100 MHz */
}

/* ---------------------------------------------------------------------- */
/* Interrupt lines                                                        */
/* ---------------------------------------------------------------------- */

static void update_mip(qp_vm *vm)
{
    uint32_t mip = 0;
    if (vm->irq_raw & vm->irq_enable) mip |= 1u << RV_IRQ_MEI;
    if (mtime_us(vm) >= vm->mtimecmp) mip |= 1u << RV_IRQ_MTI;
    vm->cpu.mip = mip;
}

/* ---------------------------------------------------------------------- */
/* Devices                                                                */
/* ---------------------------------------------------------------------- */

static void mark_flash_dirty(qp_vm *vm, uint32_t from, uint32_t to)
{
    for (uint32_t p = from >> 12; p <= (to - 1) >> 12; p++)
        vm->flash_dirty[p] = 1;
}

/* The only way to change flash: like real flash, plain stores are ignored.
 * Writes below QP_FLASH_USER_START (the core) are ignored too. */
static void flash_port_write(qp_vm *vm, uint32_t off, uint32_t v)
{
    uint32_t a = vm->flash_addr;
    switch (off) {
    case 0x0:
        vm->flash_addr = v;
        return;
    case 0x4:
    case 0x8: {
        uint32_t n = off == 0x4 ? 4 : 2;
        if (a < QP_FLASH_USER_START || a > QP_FLASH_SIZE - n) {
            vm->protected_writes++;
            return;
        }
        if (n == 4) memcpy(vm->flash + a, &v, 4);
        else { uint16_t h = (uint16_t)v; memcpy(vm->flash + a, &h, 2); }
        mark_flash_dirty(vm, a, a + n);
        return;
    }
    case 0xc:
        v &= ~3u;
        if (v < QP_FLASH_USER_START || v >= QP_FLASH_SIZE) {
            vm->protected_writes++;
            return;
        }
        memset(vm->flash + v, 0xff, QP_FLASH_SIZE - v);
        mark_flash_dirty(vm, v, QP_FLASH_SIZE);
        return;
    default:
        return;
    }
}

static void protected_write_cb(void *ctx, uint32_t addr, uint32_t pc)
{
    (void)addr; (void)pc;
    ((qp_vm *)ctx)->protected_writes++;
}

static uint32_t dev_read_word(qp_vm *vm, uint32_t a)
{
    uint32_t slot = (a >> 16) & 0xfff;
    uint32_t off = a & 0xfffc;

    switch (slot) {
    case 0x3:    /* FLASH write port */
        return off == 0 ? vm->flash_addr : 0;
    case 0x1: {  /* UART DATA */
        uint8_t b = 0;
        if (off == 0) ring_pop(&vm->rx, &b);
        return b;
    }
    case 0x2:    /* UART FLAGS */
        if (off != 0) return 0;
        return (ring_count(&vm->rx) ? 0x100u : 0)
             | (ring_count(&vm->tx) >= RING_SIZE ? 0x200u : 0);
    case 0x4:    /* SYS */
        switch (off) {
        case 0x0: return QP_BOARD_ID;
        case 0x4: return QP_BOARD_VERSION;
        case 0x8: return vm->reset_cause;
        case 0x10: return vm->safe_boot;
        default: return 0;
        }
    case 0x5:    /* IRQ */
        switch (off) {
        case 0x0: return vm->irq_raw;
        case 0x4: return vm->irq_enable;
        default: return 0;
        }
    case 0x6: {  /* TIMER */
        uint64_t t = mtime_us(vm);
        switch (off) {
        case 0x0: return (uint32_t)t;
        case 0x4: return (uint32_t)(t >> 32);
        case 0x8: return (uint32_t)vm->mtimecmp;
        case 0xc: return (uint32_t)(vm->mtimecmp >> 32);
        default: return 0;
        }
    }
    default:
        if (slot >= DEV_SLOT_FIRST && slot <= DEV_SLOT_LAST && off / 4 < DEV_WORDS)
            return vm->devregs[slot - DEV_SLOT_FIRST][off / 4];
        return 0;
    }
}

static void dev_write_word(qp_vm *vm, uint32_t a, uint32_t v)
{
    uint32_t slot = (a >> 16) & 0xfff;
    uint32_t off = a & 0xfffc;

    switch (slot) {
    case 0x3:
        flash_port_write(vm, off, v);
        return;
    case 0x1:
        if (off == 0) {
            ring_push(&vm->tx, (uint8_t)v);
            atomic_store_explicit(&vm->tx_new, 1, memory_order_release);
        }
        return;
    case 0x4:
        if (off == 0xc && (v & 1)) {
            atomic_store(&vm->reset_req, (int)QP_RESET_SOFTWARE + 1);
            vm->cpu.stop_batch = 1;
        }
        return;
    case 0x5:
        if (off == 0x4) vm->irq_enable = v & 0xfu;
        else if (off == 0x8) vm->irq_raw &= ~v;
        update_mip(vm);
        return;
    case 0x6:
        if (off == 0x8)
            vm->mtimecmp = (vm->mtimecmp & 0xffffffff00000000ull) | v;
        else if (off == 0xc)
            vm->mtimecmp = (vm->mtimecmp & 0xffffffffull) | ((uint64_t)v << 32);
        update_mip(vm);
        return;
    default:
        if (slot >= DEV_SLOT_FIRST && slot <= DEV_SLOT_LAST && off / 4 < DEV_WORDS) {
            vm->devregs[slot - DEV_SLOT_FIRST][off / 4] = v;
            if (vm->cfg.device_write)
                vm->cfg.device_write(vm->cfg.ctx, a & ~3u, v);
        }
        return;
    }
}

static int in_device_space(uint32_t a)
{
    return (a & 0xfff00000u) == QP_DEV_BASE;
}

/* Like dev_read_word, but without side effects (no UART byte consumed). */
static uint32_t dev_read_word_peek(qp_vm *vm, uint32_t a)
{
    if (((a >> 16) & 0xfff) == 0x1) return 0;
    return dev_read_word(vm, a);
}

static int mmio_read(void *ctx, uint32_t a, int size, uint32_t *v)
{
    if (!in_device_space(a)) return 1;
    uint32_t w = dev_read_word(ctx, a);
    uint32_t shift = (a & 3u) * 8u;
    if (size == 1) *v = (w >> shift) & 0xffu;
    else if (size == 2) *v = (w >> shift) & 0xffffu;
    else *v = w;
    return 0;
}

static int mmio_write(void *ctx, uint32_t a, int size, uint32_t v)
{
    (void)size;
    if (!in_device_space(a)) return 1;
    dev_write_word(ctx, a, v);
    return 0;
}

/* ---------------------------------------------------------------------- */
/* Reset                                                                  */
/* ---------------------------------------------------------------------- */

static void do_reset(qp_vm *vm, uint32_t cause, int safe)
{
    memset(vm->ram, 0, sizeof vm->ram);
    rv_reset(&vm->cpu, QP_FLASH_BASE);
    vm->irq_raw = 0;
    vm->irq_enable = QP_IRQ_BREAK;
    vm->mtimecmp = ~0ull;
    vm->reset_cause = cause;
    vm->safe_boot = safe ? 1u : 0u;
    memset(vm->devregs, 0, sizeof vm->devregs);
    vm->in_wfi = 0;
    ring_drop_all(&vm->rx);
    update_mip(vm);
}

/* ---------------------------------------------------------------------- */
/* Creation                                                               */
/* ---------------------------------------------------------------------- */

qp_vm *qp_create(const qp_config *cfg)
{
    qp_vm *vm = calloc(1, sizeof *vm);
    if (!vm) return NULL;
    if (cfg) vm->cfg = *cfg;

    pthread_mutex_init(&vm->lock, NULL);
    pthread_condattr_t ca;
    pthread_condattr_init(&ca);
    pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
    pthread_cond_init(&vm->vm_cv, &ca);
    pthread_cond_init(&vm->host_cv, &ca);
    pthread_condattr_destroy(&ca);

    memset(vm->flash, 0xff, sizeof vm->flash);

    struct rv_bus *b = &vm->bus;
    b->region[0] = (struct rv_region){
        .base = QP_FLASH_BASE, .size = QP_FLASH_SIZE, .mem = vm->flash,
        .write_floor = QP_FLASH_SIZE,   /* read-only to plain stores */
    };
    b->region[1] = (struct rv_region){
        .base = QP_RAM_BASE, .size = QP_RAM_SIZE, .mem = vm->ram,
    };
    b->nregions = 2;
    b->ctx = vm;
    b->mmio_read = mmio_read;
    b->mmio_write = mmio_write;
    b->protected_write = protected_write_cb;

    vm->cpu.bus = b;
    vm->cpu.read_cycle = read_cycle_cb;
    vm->cpu.read_time = read_time_cb;
    vm->cpu.counter_ctx = vm;

    vm->start_ns = now_ns();
    do_reset(vm, QP_RESET_POWER_ON, 0);
    return vm;
}

void qp_destroy(qp_vm *vm)
{
    if (!vm) return;
    pthread_mutex_destroy(&vm->lock);
    pthread_cond_destroy(&vm->vm_cv);
    pthread_cond_destroy(&vm->host_cv);
    free(vm);
}

/* ---------------------------------------------------------------------- */
/* Requests executed on the VM thread                                     */
/* ---------------------------------------------------------------------- */

static void run_on_vm(qp_vm *vm, void (*fn)(qp_vm *, void *), void *arg)
{
    pthread_mutex_lock(&vm->lock);
    if (!vm->running) {
        fn(vm, arg);
        pthread_mutex_unlock(&vm->lock);
        return;
    }
    while (vm->request && vm->running)
        pthread_cond_wait(&vm->host_cv, &vm->lock);
    if (!vm->running) {
        fn(vm, arg);
        pthread_mutex_unlock(&vm->lock);
        return;
    }
    struct vm_request req = { fn, arg };
    vm->request = &req;
    uint64_t seq = ++vm->request_seq;
    pthread_cond_signal(&vm->vm_cv);
    while (vm->request_done_seq < seq && vm->running)
        pthread_cond_wait(&vm->host_cv, &vm->lock);
    if (vm->request_done_seq < seq) {   /* VM stopped before serving it */
        vm->request = NULL;
        fn(vm, arg);
    }
    pthread_mutex_unlock(&vm->lock);
}

/* Called with lock held, on the VM thread. */
static void serve_request(qp_vm *vm)
{
    if (!vm->request) return;
    vm->request->fn(vm, vm->request->arg);
    vm->request = NULL;
    vm->request_done_seq = vm->request_seq;
    pthread_cond_broadcast(&vm->host_cv);
}

/* ---------------------------------------------------------------------- */
/* Flash                                                                  */
/* ---------------------------------------------------------------------- */

struct flash_io { const uint8_t *in; uint8_t *out; size_t len; int dirty; };

static void load_flash_fn(qp_vm *vm, void *arg)
{
    struct flash_io *io = arg;
    size_t n = io->len < QP_FLASH_SIZE ? io->len : QP_FLASH_SIZE;
    memcpy(vm->flash, io->in, n);
    memset(vm->flash + n, 0xff, QP_FLASH_SIZE - n);
    memset(vm->flash_dirty, 0, sizeof vm->flash_dirty);
}

void qp_load_flash(qp_vm *vm, const uint8_t *image, size_t len)
{
    struct flash_io io = { .in = image, .len = len };
    run_on_vm(vm, load_flash_fn, &io);
}

static void copy_flash_fn(qp_vm *vm, void *arg)
{
    struct flash_io *io = arg;
    io->dirty = 0;
    for (size_t i = 0; i < sizeof vm->flash_dirty; i++)
        io->dirty |= vm->flash_dirty[i];
    if (io->out) {
        memcpy(io->out, vm->flash, QP_FLASH_SIZE);
        memset(vm->flash_dirty, 0, sizeof vm->flash_dirty);
    }
}

int qp_copy_flash(qp_vm *vm, uint8_t *buf)
{
    struct flash_io io = { .out = buf };
    run_on_vm(vm, copy_flash_fn, &io);
    return io.dirty;
}

int qp_flash_dirty(qp_vm *vm)
{
    struct flash_io io = { 0 };
    run_on_vm(vm, copy_flash_fn, &io);
    return io.dirty;
}

/* ---------------------------------------------------------------------- */
/* Debug access and device inputs                                         */
/* ---------------------------------------------------------------------- */

struct mem_io { uint32_t addr; uint8_t *buf; size_t len; size_t done; };

static void read_memory_fn(qp_vm *vm, void *arg)
{
    struct mem_io *io = arg;
    io->done = rv_peek(&vm->bus, io->addr, io->buf, io->len);
}

size_t qp_read_memory(qp_vm *vm, uint32_t addr, uint8_t *buf, size_t len)
{
    struct mem_io io = { addr, buf, len, 0 };
    run_on_vm(vm, read_memory_fn, &io);
    return io.done;
}

static void snapshot_fn(qp_vm *vm, void *arg)
{
    qp_snapshot *s = arg;
    const struct rv_cpu *c = &vm->cpu;
    s->pc = c->pc;
    memcpy(s->x, c->x, sizeof s->x);
    s->mstatus = c->mstatus;
    s->mie = c->mie;
    s->mip = c->mip;
    s->mtvec = c->mtvec;
    s->mepc = c->mepc;
    s->mcause = c->mcause;
    s->mtval = c->mtval;
    s->mscratch = c->mscratch;
    s->instret = c->instret;
    s->mtime = mtime_us(vm);
    s->waiting_for_key = vm->in_wfi && ring_count(&vm->rx) == 0;
    s->paused = vm->paused;
}

void qp_snapshot_get(qp_vm *vm, qp_snapshot *out)
{
    run_on_vm(vm, snapshot_fn, out);
}

struct reg_io { uint32_t addr, value; };

static void set_reg_fn(qp_vm *vm, void *arg)
{
    struct reg_io *io = arg;
    uint32_t slot = (io->addr >> 16) & 0xfff, off = io->addr & 0xfffc;
    if (in_device_space(io->addr) && slot >= DEV_SLOT_FIRST && slot <= DEV_SLOT_LAST
        && off / 4 < DEV_WORDS)
        vm->devregs[slot - DEV_SLOT_FIRST][off / 4] = io->value;
}

void qp_set_device_reg(qp_vm *vm, uint32_t addr, uint32_t value)
{
    struct reg_io io = { addr, value };
    run_on_vm(vm, set_reg_fn, &io);
}

static void get_reg_fn(qp_vm *vm, void *arg)
{
    struct reg_io *io = arg;
    io->value = in_device_space(io->addr) ? dev_read_word_peek(vm, io->addr) : 0;
}

uint32_t qp_get_device_reg(qp_vm *vm, uint32_t addr)
{
    struct reg_io io = { addr, 0 };
    run_on_vm(vm, get_reg_fn, &io);
    return io.value;
}

struct mem_job {
    void (*fn)(const uint8_t *flash, const uint8_t *ram, void *arg);
    void *arg;
};

static void with_memory_fn(qp_vm *vm, void *arg)
{
    struct mem_job *job = arg;
    job->fn(vm->flash, vm->ram, job->arg);
}

void qp_with_memory(qp_vm *vm,
                    void (*fn)(const uint8_t *flash, const uint8_t *ram, void *arg),
                    void *arg)
{
    struct mem_job job = { fn, arg };
    run_on_vm(vm, with_memory_fn, &job);
}

struct regs_io { const uint32_t *addrs; uint32_t *values; int count; };

static void get_regs_fn(qp_vm *vm, void *arg)
{
    struct regs_io *io = arg;
    for (int i = 0; i < io->count; i++)
        io->values[i] = in_device_space(io->addrs[i]) ? dev_read_word_peek(vm, io->addrs[i]) : 0;
}

void qp_get_device_regs(qp_vm *vm, const uint32_t *addrs, uint32_t *values, int count)
{
    struct regs_io io = { addrs, values, count };
    run_on_vm(vm, get_regs_fn, &io);
}

void qp_raise_irq(qp_vm *vm, uint32_t bits)
{
    atomic_fetch_or(&vm->raise_bits, bits);
    pthread_mutex_lock(&vm->lock);
    pthread_cond_signal(&vm->vm_cv);
    pthread_mutex_unlock(&vm->lock);
}

/* ---------------------------------------------------------------------- */
/* Control                                                                */
/* ---------------------------------------------------------------------- */

void qp_shutdown(qp_vm *vm)
{
    pthread_mutex_lock(&vm->lock);
    vm->shutdown = 1;
    pthread_cond_broadcast(&vm->vm_cv);
    pthread_mutex_unlock(&vm->lock);
}

void qp_pause(qp_vm *vm, int paused)
{
    pthread_mutex_lock(&vm->lock);
    vm->paused = paused ? 1 : 0;
    pthread_cond_broadcast(&vm->vm_cv);
    pthread_mutex_unlock(&vm->lock);
}

void qp_step(qp_vm *vm)
{
    pthread_mutex_lock(&vm->lock);
    vm->step_req++;
    pthread_cond_broadcast(&vm->vm_cv);
    pthread_mutex_unlock(&vm->lock);
}

void qp_request_break(qp_vm *vm)
{
    atomic_store(&vm->break_req, 1);
    pthread_mutex_lock(&vm->lock);
    pthread_cond_broadcast(&vm->vm_cv);
    pthread_mutex_unlock(&vm->lock);
}

void qp_request_reset(qp_vm *vm, int safe_boot)
{
    atomic_store(&vm->reset_safe, safe_boot ? 1 : 0);
    atomic_store(&vm->reset_req, (int)QP_RESET_BUTTON + 1);
    pthread_mutex_lock(&vm->lock);
    pthread_cond_broadcast(&vm->vm_cv);
    pthread_mutex_unlock(&vm->lock);
}

/* ---------------------------------------------------------------------- */
/* Terminal                                                               */
/* ---------------------------------------------------------------------- */

size_t qp_uart_write(qp_vm *vm, const uint8_t *data, size_t len)
{
    size_t n = 0;
    while (n < len && ring_push(&vm->rx, data[n]))
        n++;
    if (n) {
        pthread_mutex_lock(&vm->lock);
        pthread_cond_signal(&vm->vm_cv);
        pthread_mutex_unlock(&vm->lock);
    }
    return n;
}

size_t qp_uart_read(qp_vm *vm, uint8_t *buf, size_t len)
{
    size_t n = 0;
    while (n < len && ring_pop(&vm->tx, &buf[n]))
        n++;
    return n;
}

static void deadline_after_ms(struct timespec *ts, int ms)
{
    clock_gettime(CLOCK_MONOTONIC, ts);
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) { ts->tv_sec++; ts->tv_nsec -= 1000000000L; }
}

static int ts_passed(const struct timespec *ts)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec > ts->tv_sec || (now.tv_sec == ts->tv_sec && now.tv_nsec >= ts->tv_nsec);
}

size_t qp_uart_read_wait(qp_vm *vm, uint8_t *buf, size_t len, int timeout_ms)
{
    size_t n = qp_uart_read(vm, buf, len);
    if (n || timeout_ms <= 0) return n;
    struct timespec dl;
    deadline_after_ms(&dl, timeout_ms);
    pthread_mutex_lock(&vm->lock);
    while (ring_count(&vm->tx) == 0 && !ts_passed(&dl) && !vm->shutdown)
        pthread_cond_timedwait(&vm->host_cv, &vm->lock, &dl);
    pthread_mutex_unlock(&vm->lock);
    return qp_uart_read(vm, buf, len);
}

int qp_waiting_for_key(qp_vm *vm)
{
    pthread_mutex_lock(&vm->lock);
    int w = vm->in_wfi && ring_count(&vm->rx) == 0;
    pthread_mutex_unlock(&vm->lock);
    return w;
}

int qp_wait_idle(qp_vm *vm, int timeout_ms)
{
    struct timespec dl;
    deadline_after_ms(&dl, timeout_ms);
    pthread_mutex_lock(&vm->lock);
    int idle;
    while (!(idle = vm->in_wfi && ring_count(&vm->rx) == 0) && !ts_passed(&dl) && !vm->shutdown)
        pthread_cond_timedwait(&vm->host_cv, &vm->lock, &dl);
    pthread_mutex_unlock(&vm->lock);
    return idle;
}

/* ---------------------------------------------------------------------- */
/* Main loop                                                              */
/* ---------------------------------------------------------------------- */

/* Lock held. Applies asynchronous requests from host threads. */
static void apply_requests(qp_vm *vm)
{
    int r = atomic_exchange(&vm->reset_req, 0);
    if (r) {
        do_reset(vm, (uint32_t)(r - 1), atomic_exchange(&vm->reset_safe, 0));
        atomic_store(&vm->break_req, 0);
    }
    if (atomic_exchange(&vm->break_req, 0))
        vm->irq_raw |= QP_IRQ_BREAK;
    uint32_t raise = atomic_exchange(&vm->raise_bits, 0);
    vm->irq_raw |= raise & 0xfu;
    update_mip(vm);
}

static void notify_output(qp_vm *vm)
{
    if (atomic_exchange(&vm->tx_new, 0)) {
        pthread_cond_broadcast(&vm->host_cv);
        if (vm->cfg.uart_output)
            vm->cfg.uart_output(vm->cfg.ctx);
    }
}

/* Lock held. Sleeps in wfi until something happens. Returns when the VM
 * should run again (or re-check its state). */
static void wfi_wait(qp_vm *vm)
{
    if (!vm->in_wfi) {
        vm->in_wfi = 1;
        pthread_cond_broadcast(&vm->host_cv);   /* idle waiters */
    }

    int timer_armed = (vm->cpu.mie & (1u << RV_IRQ_MTI)) && vm->mtimecmp != ~0ull;
    uint64_t now = mtime_us(vm);

    if (vm->cfg.virtual_time && timer_armed && vm->mtimecmp > now
        && ring_count(&vm->rx) == 0) {
        /* Nothing else can happen in a scripted run: jump to the deadline. */
        vm->virtual_offset_us += vm->mtimecmp - now;
        return;
    }

    struct timespec dl;
    int ms = MAX_WAIT_MS;
    if (timer_armed && !vm->cfg.virtual_time) {
        uint64_t left = vm->mtimecmp > now ? vm->mtimecmp - now : 0;
        if (left / 1000 < (uint64_t)ms) ms = (int)(left / 1000);
    }
    if (ms > 0) {
        deadline_after_ms(&dl, ms);
        pthread_cond_timedwait(&vm->vm_cv, &vm->lock, &dl);
    } else if (timer_armed) {
        /* Sub-millisecond deadline: spin briefly without the lock. */
        pthread_mutex_unlock(&vm->lock);
        struct timespec ts = { 0, 50000 };
        nanosleep(&ts, NULL);
        pthread_mutex_lock(&vm->lock);
    }
}

void qp_run(qp_vm *vm)
{
    pthread_mutex_lock(&vm->lock);
    vm->running = 1;
    vm->shutdown = 0;
    do_reset(vm, QP_RESET_POWER_ON, atomic_exchange(&vm->reset_safe, 0));

    for (;;) {
        serve_request(vm);
        notify_output(vm);
        if (vm->shutdown) break;
        apply_requests(vm);

        if (vm->paused) {
            if (!vm->paused_since_ns) vm->paused_since_ns = now_ns();
            if (vm->step_req > 0) {
                vm->step_req--;
                vm->cpu.wfi = 0;
                vm->in_wfi = 0;
                rv_run(&vm->cpu, 1);
                pthread_cond_broadcast(&vm->host_cv);
                continue;
            }
            pthread_cond_wait(&vm->vm_cv, &vm->lock);
            continue;
        }
        if (vm->paused_since_ns) {
            vm->paused_total_ns += now_ns() - vm->paused_since_ns;
            vm->paused_since_ns = 0;
        }

        if (vm->cpu.wfi) {
            if (rv_wake_pending(&vm->cpu) || ring_count(&vm->rx) != 0) {
                vm->cpu.wfi = 0;
                vm->in_wfi = 0;
            } else {
                wfi_wait(vm);
                continue;
            }
        }

        pthread_mutex_unlock(&vm->lock);
        rv_run(&vm->cpu, BATCH);
        pthread_mutex_lock(&vm->lock);
    }

    vm->running = 0;
    vm->in_wfi = 0;
    if (vm->request) serve_request(vm);
    pthread_cond_broadcast(&vm->host_cv);
    pthread_mutex_unlock(&vm->lock);
}
