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
#define DEV_SLOT_FIRST 0xAu          /* torch (reserved) */
#define DEV_SLOT_LAST  0xBu          /* sensors (reserved) */
#define DEV_WORDS    64u             /* registers per generic device slot */
#define EVENT_RING   1024u           /* power of two */
#define TONE_EVENT_GAP_US 2000u      /* at most one tone update per 2 ms */
#define MAX_WAIT_MS  100
#define TAIL_SIZE    256u            /* power of two: recent output kept for qp_send_line */

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

/* Device events, VM thread to host: single producer, single consumer. */
struct event_ring {
    qp_event ev[EVENT_RING];
    _Atomic uint32_t head;
    _Atomic uint32_t tail;
};

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

    /* Phone devices (VM thread) */
    uint64_t vib_end_us;          /* 0: not vibrating */
    int32_t brightness;           /* -1: system setting */
    uint32_t audio_freq, audio_wave, audio_volume;
    uint64_t audio_end_us;        /* 0: silent */
    uint64_t tone_event_us;       /* when the last tone event went out */
    int tone_dirty;               /* a tone change still has to go out */
    struct event_ring events;

    /* GPIO (VM thread; the host goes through run_on_vm) */
    uint32_t gpio_dir, gpio_out, gpio_ext, gpio_levels;
    uint64_t gpio_changes;        /* trace entries ever written */
    qp_gpio_change gpio_trace[QP_GPIO_TRACE_SIZE];

    struct ring rx, tx;
    _Atomic int tx_new;
    uint8_t tail[TAIL_SIZE];      /* last output bytes (VM thread) */
    uint64_t tx_total;            /* bytes ever output (VM thread) */

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

static void device_timers(qp_vm *vm);

static void update_mip(qp_vm *vm)
{
    device_timers(vm);
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

/* Phone devices ---------------------------------------------------------- */

static void push_event(qp_vm *vm, int32_t type, int32_t a, int32_t b, int32_t c, int32_t d)
{
    struct event_ring *r = &vm->events;
    uint32_t h = atomic_load_explicit(&r->head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&r->tail, memory_order_acquire);
    if (h - t >= EVENT_RING) return;   /* host not listening: drop */
    r->ev[h & (EVENT_RING - 1)] = (qp_event){ type, a, b, c, d };
    atomic_store_explicit(&r->head, h + 1, memory_order_release);
}

static uint32_t ms_left(uint64_t end_us, uint64_t now)
{
    return end_us > now ? (uint32_t)((end_us - now + 999) / 1000) : 0;
}

static void push_tone(qp_vm *vm, uint64_t now)
{
    push_event(vm, QP_EVENT_TONE, (int32_t)vm->audio_freq,
               vm->audio_end_us ? (int32_t)ms_left(vm->audio_end_us, now) : 0,
               (int32_t)vm->audio_wave, (int32_t)vm->audio_volume);
    vm->tone_event_us = now;
    vm->tone_dirty = 0;
}

/* A new frequency, waveform or volume while a tone plays: tell the host,
 * but not more often than every TONE_EVENT_GAP_US (a sweep in a loop would
 * flood the event ring). The last change goes out from flush_tone. */
static void tone_changed(qp_vm *vm)
{
    uint64_t now = mtime_us(vm);
    if (!vm->audio_end_us || now >= vm->audio_end_us) return;
    if (now - vm->tone_event_us >= TONE_EVENT_GAP_US) push_tone(vm, now);
    else vm->tone_dirty = 1;
}

static void flush_tone(qp_vm *vm, int force)
{
    if (!vm->tone_dirty) return;
    uint64_t now = mtime_us(vm);
    if (!vm->audio_end_us || now >= vm->audio_end_us) { vm->tone_dirty = 0; return; }
    if (force || now - vm->tone_event_us >= TONE_EVENT_GAP_US) push_tone(vm, now);
}

/* Called from update_mip: a tone that ran out raises the audio interrupt. */
static void device_timers(qp_vm *vm)
{
    if (vm->audio_end_us && mtime_us(vm) >= vm->audio_end_us) {
        vm->audio_end_us = 0;
        vm->tone_dirty = 0;
        vm->irq_raw |= QP_IRQ_AUDIO;
    }
}

static void gpio_record(qp_vm *vm, uint32_t changed, uint32_t levels)
{
    uint64_t t = mtime_us(vm);
    uint64_t tick = t - t % QP_GPIO_TICK_US;
    if (vm->gpio_changes > 0) {
        qp_gpio_change *e = &vm->gpio_trace[(vm->gpio_changes - 1) % QP_GPIO_TRACE_SIZE];
        if (e->t_us == tick) {
            e->busy |= (uint16_t)(e->toggled & changed);
            e->toggled |= (uint16_t)changed;
            e->levels = (uint16_t)levels;
            return;
        }
    }
    vm->gpio_trace[vm->gpio_changes % QP_GPIO_TRACE_SIZE] =
        (qp_gpio_change){ tick, (uint16_t)levels, (uint16_t)changed, 0,
                          (uint16_t)(levels ^ changed) };
    vm->gpio_changes++;
}

static void gpio_update(qp_vm *vm)
{
    uint32_t levels = ((vm->gpio_out & vm->gpio_dir) | (vm->gpio_ext & ~vm->gpio_dir)) & QP_GPIO_MASK;
    if (levels != vm->gpio_levels) {
        gpio_record(vm, levels ^ vm->gpio_levels, levels);
        vm->gpio_levels = levels;
    }
}

static uint32_t devices_read(qp_vm *vm, uint32_t slot, uint32_t off)
{
    switch (slot) {
    case 0x7:   /* vibrator */
        return off == 0 && vm->vib_end_us ? ms_left(vm->vib_end_us, mtime_us(vm)) : 0;
    case 0x8:   /* display */
        return off == 0 ? (uint32_t)vm->brightness : 0;
    case 0x9:   /* audio */
        switch (off) {
        case 0x0: return vm->audio_freq;
        case 0x4: return vm->audio_end_us ? ms_left(vm->audio_end_us, mtime_us(vm)) : 0;
        case 0x8: return vm->audio_wave;
        case 0xc: return vm->audio_volume;
        default: return 0;
        }
    case 0xC:   /* GPIO */
        switch (off) {
        case 0x0: return vm->gpio_dir;
        case 0x4: return vm->gpio_out;
        case 0x8: return vm->gpio_levels;
        default: return 0;
        }
    default:
        return 0;
    }
}

static void devices_write(qp_vm *vm, uint32_t slot, uint32_t off, uint32_t v)
{
    switch (slot) {
    case 0x7:
        if (off == 0) {
            if (v > 60000) v = 60000;
            vm->vib_end_us = v ? mtime_us(vm) + (uint64_t)v * 1000 : 0;
            push_event(vm, QP_EVENT_VIBRATE, (int32_t)v, 0, 0, 0);
        }
        return;
    case 0x8:
        if (off == 0) {
            int32_t b = (int32_t)v < 0 ? -1 : (v > 255 ? 255 : (int32_t)v);
            vm->brightness = b;
            push_event(vm, QP_EVENT_BRIGHTNESS, b, 0, 0, 0);
        }
        return;
    case 0x9:
        switch (off) {
        case 0x0: vm->audio_freq = v > 20000 ? 20000 : v; tone_changed(vm); return;
        case 0x8: vm->audio_wave = v & 3; tone_changed(vm); return;
        case 0xc: vm->audio_volume = v > 100 ? 100 : v; tone_changed(vm); return;
        case 0x4: {
            uint64_t now = mtime_us(vm);
            if (v > 3600000) v = 3600000;   /* an hour at most */
            vm->audio_end_us = v ? now + (uint64_t)v * 1000 : 0;
            push_tone(vm, now);
            return;
        }
        default: return;
        }
    case 0xC:
        switch (off) {
        case 0x00: vm->gpio_dir = v & QP_GPIO_MASK; break;
        case 0x04: vm->gpio_out = v & QP_GPIO_MASK; break;
        case 0x0c: vm->gpio_out |= v & QP_GPIO_MASK; break;
        case 0x10: vm->gpio_out &= ~v; break;
        case 0x14: vm->gpio_out ^= v & QP_GPIO_MASK; break;
        default: return;
        }
        gpio_update(vm);
        return;
    default:
        return;
    }
}

/* Reset: devices back to their power-on state; the phone stops what the
 * board started. External input levels stay: they belong to the outside. */
static void devices_reset(qp_vm *vm)
{
    uint64_t now = mtime_us(vm);
    if (vm->vib_end_us && vm->vib_end_us > now) push_event(vm, QP_EVENT_VIBRATE, 0, 0, 0, 0);
    if (vm->brightness != -1) push_event(vm, QP_EVENT_BRIGHTNESS, -1, 0, 0, 0);
    vm->vib_end_us = 0;
    vm->brightness = -1;
    int was_playing = vm->audio_end_us && vm->audio_end_us > now;
    vm->audio_end_us = 0;
    vm->audio_freq = 440;
    vm->audio_wave = QP_WAVE_SINE;
    vm->audio_volume = 50;
    vm->tone_dirty = 0;
    if (was_playing) push_tone(vm, now);
    vm->gpio_dir = 0;
    vm->gpio_out = 0;
    gpio_update(vm);
}

/* Board registers ---------------------------------------------------------- */

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
    case 0x7: case 0x8: case 0x9: case 0xC:
        return devices_read(vm, slot, off);
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
            vm->tail[vm->tx_total & (TAIL_SIZE - 1)] = (uint8_t)v;
            vm->tx_total++;
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
    case 0x7: case 0x8: case 0x9: case 0xC:
        devices_write(vm, slot, off, v);
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
    devices_reset(vm);
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
    vm->brightness = -1;   /* nothing for the phone to undo at power-on */
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

size_t qp_poll_events(qp_vm *vm, qp_event *out, size_t max)
{
    struct event_ring *r = &vm->events;
    uint32_t t = atomic_load_explicit(&r->tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&r->head, memory_order_acquire);
    size_t n = 0;
    while (t != h && n < max) out[n++] = r->ev[t++ & (EVENT_RING - 1)];
    atomic_store_explicit(&r->tail, t, memory_order_release);
    return n;
}

static void gpio_get_fn(qp_vm *vm, void *arg)
{
    qp_gpio_state *st = arg;
    st->dir = vm->gpio_dir;
    st->out = vm->gpio_out;
    st->ext = vm->gpio_ext;
    st->levels = vm->gpio_levels;
    st->now_us = mtime_us(vm);
    st->changes = vm->gpio_changes;
}

void qp_gpio_get(qp_vm *vm, qp_gpio_state *out)
{
    run_on_vm(vm, gpio_get_fn, out);
}

struct gpio_input { uint32_t mask, levels; };

static void gpio_set_input_fn(qp_vm *vm, void *arg)
{
    struct gpio_input *in = arg;
    vm->gpio_ext = ((vm->gpio_ext & ~in->mask) | (in->levels & in->mask)) & QP_GPIO_MASK;
    gpio_update(vm);
}

void qp_gpio_set_input(qp_vm *vm, uint32_t mask, uint32_t levels)
{
    struct gpio_input in = { mask, levels };
    run_on_vm(vm, gpio_set_input_fn, &in);
}

struct gpio_trace_io { uint64_t from; qp_gpio_change *out; size_t max, n; uint64_t first; };

static void gpio_trace_fn(qp_vm *vm, void *arg)
{
    struct gpio_trace_io *io = arg;
    uint64_t end = vm->gpio_changes;
    uint64_t start = io->from;
    uint64_t oldest = end > QP_GPIO_TRACE_SIZE ? end - QP_GPIO_TRACE_SIZE : 0;
    if (start < oldest) start = oldest;
    if (end > start && end - start > io->max) start = end - io->max;
    if (start > end) start = end;
    io->first = start;
    io->n = 0;
    for (uint64_t i = start; i < end; i++)
        io->out[io->n++] = vm->gpio_trace[i % QP_GPIO_TRACE_SIZE];
}

size_t qp_gpio_trace(qp_vm *vm, uint64_t from, qp_gpio_change *out, size_t max,
                     uint64_t *first)
{
    struct gpio_trace_io io = { from, out, max, 0, 0 };
    run_on_vm(vm, gpio_trace_fn, &io);
    if (first) *first = io.first;
    return io.n;
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

struct tail_io { uint64_t total; uint8_t bytes[TAIL_SIZE]; };

static void tail_fn(qp_vm *vm, void *arg)
{
    struct tail_io *io = arg;
    io->total = vm->tx_total;
    for (uint32_t i = 0; i < TAIL_SIZE; i++)   /* oldest first */
        io->bytes[i] = vm->tail[(vm->tx_total + i) & (TAIL_SIZE - 1)];
}

int qp_send_line(qp_vm *vm, const char *line, size_t len, int timeout_ms)
{
    struct tail_io before, after;
    run_on_vm(vm, tail_fn, &before);

    struct timespec dl;
    deadline_after_ms(&dl, timeout_ms);
    size_t off = 0;
    const char nl = '\n';
    while (off < len) {
        size_t n = qp_uart_write(vm, (const uint8_t *)line + off, len - off);
        off += n;
        if (!n) {
            if (ts_passed(&dl)) return QP_LINE_TIMEOUT;
            struct timespec ts = { 0, 1000000L };
            nanosleep(&ts, NULL);
        }
    }
    while (!qp_uart_write(vm, (const uint8_t *)&nl, 1)) {
        if (ts_passed(&dl)) return QP_LINE_TIMEOUT;
        struct timespec ts = { 0, 1000000L };
        nanosleep(&ts, NULL);
    }

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long left = (long)(dl.tv_sec - now.tv_sec) * 1000 + (dl.tv_nsec - now.tv_nsec) / 1000000;
    if (left <= 0 || !qp_wait_idle(vm, (int)left)) return QP_LINE_TIMEOUT;

    run_on_vm(vm, tail_fn, &after);
    uint64_t produced = after.total - before.total;
    if (produced == 0) return QP_LINE_ERROR;
    if (produced > TAIL_SIZE) produced = TAIL_SIZE;

    /* Output of this line, without trailing line end and spaces. */
    const uint8_t *out = after.bytes + (TAIL_SIZE - produced);
    size_t n = (size_t)produced;
    while (n && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' ')) n--;
    if (n >= 4 && !memcmp(out + n - 4, " ok.", 4)) return QP_LINE_OK;
    return QP_LINE_ERROR;
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

    flush_tone(vm, 1);

    int timer_armed = (vm->cpu.mie & (1u << RV_IRQ_MTI)) && vm->mtimecmp != ~0ull;
    uint64_t now = mtime_us(vm);
    /* A tone running out raises the audio interrupt: wake up for it. */
    int audio_armed = (vm->cpu.mie & (1u << RV_IRQ_MEI)) && (vm->irq_enable & QP_IRQ_AUDIO)
                      && vm->audio_end_us;

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
    if (audio_armed && !vm->cfg.virtual_time) {
        uint64_t left = vm->audio_end_us > now ? vm->audio_end_us - now : 0;
        if ((left + 999) / 1000 < (uint64_t)ms) ms = (int)((left + 999) / 1000);
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
        flush_tone(vm, 0);

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
