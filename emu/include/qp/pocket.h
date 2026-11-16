/*
 * Mecrisp Pocket - the virtual board: RV32IM core, flash, RAM, devices.
 * Copyright (C) 2026 The Mecrisp Pocket authors. GPL-3.0-or-later.
 *
 * Threading model: one thread calls qp_run(), which executes guest code
 * until qp_shutdown(). Every other function may be called from any thread.
 * See docs/board.md for the memory map and device registers.
 */
#ifndef QP_POCKET_H
#define QP_POCKET_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QP_FLASH_BASE        0x00000000u
#define QP_FLASH_SIZE        0x00040000u   /* 256 KiB */
#define QP_FLASH_USER_START  0x00008000u   /* core lives below, read-only */
#define QP_RAM_BASE          0x00040000u
#define QP_RAM_SIZE          0x00040000u   /* 256 KiB */
#define QP_DEV_BASE          0x40000000u

#define QP_BOARD_ID          0x51504b54u   /* "QPKT" */
#define QP_BOARD_VERSION     1u

/* IRQ controller bits */
#define QP_IRQ_BREAK   (1u << 0)
#define QP_IRQ_UART_RX (1u << 1)
#define QP_IRQ_AUDIO   (1u << 2)
#define QP_IRQ_GPIO    (1u << 3)

/* Reset causes (SYS RESET CAUSE) */
#define QP_RESET_POWER_ON 0u
#define QP_RESET_BUTTON   1u
#define QP_RESET_SOFTWARE 2u

typedef struct qp_vm qp_vm;

typedef struct {
    /* Nonzero: time and cycle counters follow the instruction count
     * (reproducible test runs). Zero: real time. */
    int virtual_time;
} qp_config;

typedef struct {
    uint32_t pc;
    uint32_t x[32];
    uint32_t mstatus, mie, mip, mtvec, mepc, mcause, mtval, mscratch;
    uint64_t instret;
    uint64_t mtime;
    int waiting_for_key;
    int paused;
} qp_snapshot;

qp_vm *qp_create(const qp_config *cfg);
void qp_destroy(qp_vm *vm);

/* Flash image access. qp_load_flash copies up to QP_FLASH_SIZE bytes;
 * the rest is filled with 0xFF. Safe to call only while qp_run is not
 * executing guest code (before qp_run, or while paused). */
void qp_load_flash(qp_vm *vm, const uint8_t *image, size_t len);
/* Copies the whole flash into buf (QP_FLASH_SIZE bytes) and clears the
 * dirty flag. Returns nonzero if flash had changed since the last copy. */
int qp_copy_flash(qp_vm *vm, uint8_t *buf);
int qp_flash_dirty(qp_vm *vm);

/* Runs guest code on the calling thread until qp_shutdown(). Starts with
 * a power-on reset. */
void qp_run(qp_vm *vm);
void qp_shutdown(qp_vm *vm);

void qp_pause(qp_vm *vm, int paused);
/* Executes one instruction while paused. */
void qp_step(qp_vm *vm);
/* Stop button: raises the break interrupt. */
void qp_request_break(qp_vm *vm);
/* Reset button. safe_boot = 1 sets SYS SAFE BOOT so init is skipped. */
void qp_request_reset(qp_vm *vm, int safe_boot);

/* Terminal. qp_uart_write queues input bytes and returns how many fit.
 * qp_uart_read drains output; returns the number of bytes copied.
 * qp_uart_read_wait blocks up to timeout_ms for output. */
size_t qp_uart_write(qp_vm *vm, const uint8_t *data, size_t len);
size_t qp_uart_read(qp_vm *vm, uint8_t *buf, size_t len);
size_t qp_uart_read_wait(qp_vm *vm, uint8_t *buf, size_t len, int timeout_ms);

/* True while the guest sleeps in wfi with no input waiting: Forth is idle
 * at the prompt (or in key). */
int qp_waiting_for_key(qp_vm *vm);
/* Blocks until the guest is idle in wfi with no input waiting, or timeout.
 * Returns 1 if idle. */
int qp_wait_idle(qp_vm *vm, int timeout_ms);

/* Result of qp_send_line. */
#define QP_LINE_OK      0   /* Forth answered " ok." */
#define QP_LINE_ERROR   1   /* Forth printed an error instead */
#define QP_LINE_TIMEOUT 2   /* Forth did not return to the prompt in time */
#define QP_LINE_TOO_LONG 3  /* longer than QP_MAX_LINE bytes: not sent */

/* Forth's input buffer (tiblength in quintus/pocket): a longer line would
 * be cut off without an error. */
#define QP_MAX_LINE 1024

/* Sends one line of source (without line end) and waits until Forth is
 * idle again, then checks whether its output for this line ended with
 * " ok.". This is how a file is loaded line by line, like e4thcom does.
 * Output stays in the UART buffer for the caller to display. */
int qp_send_line(qp_vm *vm, const char *line, size_t len, int timeout_ms);

/* Debug access: memory reads have no device side effects. */
size_t qp_read_memory(qp_vm *vm, uint32_t addr, uint8_t *buf, size_t len);
void qp_snapshot_get(qp_vm *vm, qp_snapshot *out);

/* Runs fn on the VM thread with direct read access to flash and RAM
 * (used by the dictionary reader in dict.c). */
void qp_with_memory(qp_vm *vm,
                    void (*fn)(const uint8_t *flash, const uint8_t *ram, void *arg),
                    void *arg);

/* Reads several device registers at once, without side effects. */
void qp_get_device_regs(qp_vm *vm, const uint32_t *addrs, uint32_t *values, int count);

/* ---------------------------------------------------------------------- */
/* Phone devices and GPIO (docs/board.md)                                 */
/* ---------------------------------------------------------------------- */

#define QP_VIB_BASE      0x40070000u  /* +0 TIME: write n ms to vibrate (0 stops); read ms left */
#define QP_DISP_BASE     0x40080000u  /* +0 BRIGHTNESS: 0..255, or -1 for the system setting */
#define QP_AUDIO_BASE    0x40090000u  /* +0 FREQ (Hz), +4 TIME (write n ms to play, 0 stops;
                                         read ms left), +8 WAVE, +C VOLUME (0..100) */
#define QP_GPIO_BASE     0x400C0000u  /* +0 DIR (1 = output), +4 OUT, +8 IN, +C SET, +10 CLR,
                                         +14 TOGGLE */
#define QP_GPIO_PINS     16
#define QP_GPIO_MASK     0xffffu

#define QP_WAVE_SINE     0
#define QP_WAVE_SQUARE   1
#define QP_WAVE_TRIANGLE 2
#define QP_WAVE_SAWTOOTH 3

/* What the board asks of the phone, as its current state. The phone polls
 * it and acts on what changed: a new vibrate_seq or tone_seq, or a new
 * brightness. Only the latest state counts, so nothing can pile up. */
typedef struct {
    uint32_t vibrate_seq;   /* +1 on every write to VIB TIME */
    int32_t vibrate_ms;     /* as written: 0 stops */
    int32_t brightness;     /* 0..255, or -1 for the system setting */
    uint32_t tone_seq;      /* +1 when a tone starts or stops, and when its
                               frequency, waveform or volume change */
    int32_t tone_hz;
    int32_t tone_ms;        /* ms left at that moment; 0: silent */
    int32_t tone_wave;
    int32_t tone_volume;
} qp_phone;

/* Any thread; does not wake the board. */
void qp_phone_get(qp_vm *vm, qp_phone *out);

typedef struct {
    uint32_t dir;       /* 1 = output */
    uint32_t out;       /* output latch */
    uint32_t ext;       /* levels driven from outside, seen on input pins */
    uint32_t levels;    /* what IN reads: out on outputs, ext on inputs */
    uint64_t now_us;    /* board time */
    uint64_t changes;   /* index the next trace entry will get */
} qp_gpio_state;

void qp_gpio_get(qp_vm *vm, qp_gpio_state *out);

/* Drives input pins from outside (a tap in the GPIO view, a USB breakout):
 * the pins in mask get the levels in levels. */
void qp_gpio_set_input(qp_vm *vm, uint32_t mask, uint32_t levels);

/* The trace records pin level changes. Changes within one tick of
 * QP_GPIO_TICK_US share an entry, so fast toggling cannot flood it. */
#define QP_GPIO_TICK_US     100u
#define QP_GPIO_TRACE_SIZE  65536u   /* entries kept */

typedef struct {
    uint64_t t_us;      /* start of the tick */
    uint16_t levels;    /* levels at the end of the tick */
    uint16_t toggled;   /* pins that changed in the tick */
    uint16_t busy;      /* pins that changed more than once in it */
    uint16_t before;    /* levels at the start of the tick */
} qp_gpio_change;

/* Copies trace entries from index `from` on: at most max, the newest ones,
 * and only those still kept. *first gets the index of out[0]. The newest
 * entry can still change while its tick lasts, so read it again next time. */
size_t qp_gpio_trace(qp_vm *vm, uint64_t from, qp_gpio_change *out, size_t max,
                     uint64_t *first);

#ifdef __cplusplus
}
#endif

#endif
