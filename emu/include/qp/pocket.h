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
    /* Called on the VM thread for writes to device slots 0x4007..0x400C
     * (vibrator, display, audio, torch, sensors, GPIO). Must not block. */
    void (*device_write)(void *ctx, uint32_t addr, uint32_t value);
    /* Called on the VM thread when new UART output is available. Must not
     * block. May be NULL; readers can also poll qp_uart_read(). */
    void (*uart_output)(void *ctx);
    void *ctx;
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

/* Debug access: memory reads have no device side effects. */
size_t qp_read_memory(qp_vm *vm, uint32_t addr, uint8_t *buf, size_t len);
void qp_snapshot_get(qp_vm *vm, qp_snapshot *out);

/* Runs fn on the VM thread with direct read access to flash and RAM. */
void qp_with_memory(qp_vm *vm,
                    void (*fn)(const uint8_t *flash, const uint8_t *ram, void *arg),
                    void *arg);

/* Reads several device registers at once, without side effects. */
void qp_get_device_regs(qp_vm *vm, const uint32_t *addrs, uint32_t *values, int count);

/* Device inputs from the host side. */
void qp_raise_irq(qp_vm *vm, uint32_t bits);
void qp_set_device_reg(qp_vm *vm, uint32_t addr, uint32_t value);
uint32_t qp_get_device_reg(qp_vm *vm, uint32_t addr);

#ifdef __cplusplus
}
#endif

#endif
