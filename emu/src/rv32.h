/*
 * Mecrisp Pocket - RV32IM CPU core (machine mode only).
 * Copyright (C) 2026 The Mecrisp Pocket authors. GPL-3.0-or-later.
 *
 * The core knows nothing about the board: memory regions and an MMIO
 * callback are supplied through struct rv_bus.
 */
#ifndef QP_RV32_H
#define QP_RV32_H

#include <stdint.h>
#include <stddef.h>

#define RV_MAX_REGIONS 4

/* Exception and interrupt causes */
enum {
    RV_EXC_INSN_MISALIGNED = 0,
    RV_EXC_INSN_FAULT = 1,
    RV_EXC_ILLEGAL = 2,
    RV_EXC_BREAKPOINT = 3,
    RV_EXC_LOAD_FAULT = 5,
    RV_EXC_STORE_FAULT = 7,
    RV_EXC_ECALL_M = 11,
};

#define RV_IRQ_MSI 3
#define RV_IRQ_MTI 7
#define RV_IRQ_MEI 11

#define RV_MSTATUS_MIE  (1u << 3)
#define RV_MSTATUS_MPIE (1u << 7)
#define RV_MSTATUS_MPP  (3u << 11)

struct rv_region {
    uint32_t base;
    uint32_t size;
    uint8_t *mem;
    uint32_t write_floor;  /* writes below base+write_floor are ignored */
    uint8_t *dirty;        /* optional: one byte per 4 KiB page, set on write */
};

struct rv_bus {
    struct rv_region region[RV_MAX_REGIONS];
    int nregions;
    void *ctx;
    /* MMIO for addresses outside all regions. Return 0 on success,
     * nonzero for an access fault. size is 1, 2 or 4. */
    int (*mmio_read)(void *ctx, uint32_t addr, int size, uint32_t *value);
    int (*mmio_write)(void *ctx, uint32_t addr, int size, uint32_t value);
    /* Called for writes that hit a region's protected area (may be NULL). */
    void (*protected_write)(void *ctx, uint32_t addr, uint32_t pc);
};

struct rv_cpu {
    uint32_t x[32];
    uint32_t pc;

    uint32_t mstatus, mie, mip, mtvec, mepc, mcause, mtval, mscratch;
    uint64_t instret;

    /* Supplies the cycle and time counters (nominal clock). */
    uint64_t (*read_cycle)(void *ctx);
    uint64_t (*read_time)(void *ctx);
    void *counter_ctx;

    int wfi;          /* set when a wfi instruction executes; cleared by caller */
    int stop_batch;   /* set by bus callbacks to end the current batch early */

    struct rv_bus *bus;
};

void rv_reset(struct rv_cpu *cpu, uint32_t pc);

/* Runs up to n instructions. Returns the number executed. Stops early on
 * wfi or when stop_batch is set. Pending enabled interrupts are taken
 * before each instruction. */
uint32_t rv_run(struct rv_cpu *cpu, uint32_t n);

/* True if an interrupt is both pending and enabled in mie (ignores MIE),
 * which is the wake-up condition for wfi. */
int rv_wake_pending(const struct rv_cpu *cpu);

/* Debug read of guest memory regions (no MMIO side effects). Returns the
 * number of bytes copied; bytes outside regions read as zero. */
size_t rv_peek(const struct rv_bus *bus, uint32_t addr, uint8_t *buf, size_t len);

#endif
