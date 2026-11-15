/*
 * Mecrisp Pocket - RV32IM CPU core (machine mode only).
 * Copyright (C) 2026 The Mecrisp Pocket authors. GPL-3.0-or-later.
 */
#include "rv32.h"

#include <string.h>

#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

void rv_reset(struct rv_cpu *c, uint32_t pc)
{
    memset(c->x, 0, sizeof c->x);
    c->pc = pc;
    c->mstatus = RV_MSTATUS_MPP;
    c->mie = 0;
    c->mip = 0;
    c->mtvec = 0;
    c->mepc = 0;
    c->mcause = 0;
    c->mtval = 0;
    c->mscratch = 0;
    c->instret = 0;
    c->wfi = 0;
    c->stop_batch = 0;
}

int rv_wake_pending(const struct rv_cpu *c)
{
    return (c->mip & c->mie) != 0;
}

/* ---------------------------------------------------------------------- */
/* Memory access                                                          */
/* ---------------------------------------------------------------------- */

static inline struct rv_region *region_for(struct rv_bus *b, uint32_t a, uint32_t len)
{
    for (int i = 0; i < b->nregions; i++) {
        struct rv_region *r = &b->region[i];
        uint32_t off = a - r->base;
        if (off < r->size && r->size - off >= len)
            return r;
    }
    return NULL;
}

size_t rv_peek(const struct rv_bus *b, uint32_t addr, uint8_t *buf, size_t len)
{
    size_t done = 0;
    while (done < len) {
        uint32_t a = addr + (uint32_t)done;
        const struct rv_region *hit = NULL;
        for (int i = 0; i < b->nregions; i++) {
            const struct rv_region *r = &b->region[i];
            if (a - r->base < r->size) { hit = r; break; }
        }
        if (hit) {
            uint32_t off = a - hit->base;
            size_t n = hit->size - off;
            if (n > len - done) n = len - done;
            memcpy(buf + done, hit->mem + off, n);
            done += n;
        } else {
            buf[done++] = 0;
        }
    }
    return done;
}

/* Returns 0 on success, nonzero on access fault. */
static inline int load(struct rv_cpu *c, uint32_t a, int size, uint32_t *v)
{
    struct rv_region *r = region_for(c->bus, a, (uint32_t)size);
    if (LIKELY(r != NULL)) {
        const uint8_t *p = r->mem + (a - r->base);
        switch (size) {
        case 1: *v = p[0]; break;
        case 2: { uint16_t h; memcpy(&h, p, 2); *v = h; break; }
        default: memcpy(v, p, 4); break;
        }
        return 0;
    }
    if (c->bus->mmio_read)
        return c->bus->mmio_read(c->bus->ctx, a, size, v);
    return 1;
}

static inline int store(struct rv_cpu *c, uint32_t a, int size, uint32_t v)
{
    struct rv_region *r = region_for(c->bus, a, (uint32_t)size);
    if (LIKELY(r != NULL)) {
        uint32_t off = a - r->base;
        if (UNLIKELY(off < r->write_floor)) {
            if (c->bus->protected_write)
                c->bus->protected_write(c->bus->ctx, a, c->pc);
            return 0;
        }
        uint8_t *p = r->mem + off;
        switch (size) {
        case 1: p[0] = (uint8_t)v; break;
        case 2: { uint16_t h = (uint16_t)v; memcpy(p, &h, 2); break; }
        default: memcpy(p, &v, 4); break;
        }
        if (r->dirty) {
            r->dirty[off >> 12] = 1;
            r->dirty[(off + (uint32_t)size - 1) >> 12] = 1;
        }
        return 0;
    }
    if (c->bus->mmio_write)
        return c->bus->mmio_write(c->bus->ctx, a, size, v);
    return 1;
}

static inline int fetch(struct rv_cpu *c, uint32_t a, uint32_t *insn)
{
    struct rv_region *r = region_for(c->bus, a, 4);
    if (LIKELY(r != NULL)) {
        memcpy(insn, r->mem + (a - r->base), 4);
        return 0;
    }
    return 1;
}

/* ---------------------------------------------------------------------- */
/* Traps                                                                  */
/* ---------------------------------------------------------------------- */

static void trap(struct rv_cpu *c, uint32_t cause, uint32_t tval)
{
    uint32_t s = c->mstatus;
    c->mepc = c->pc;
    c->mcause = cause;
    c->mtval = tval;
    s = (s & ~RV_MSTATUS_MPIE) | ((s & RV_MSTATUS_MIE) ? RV_MSTATUS_MPIE : 0);
    s &= ~RV_MSTATUS_MIE;
    s |= RV_MSTATUS_MPP;
    c->mstatus = s;

    uint32_t base = c->mtvec & ~3u;
    if ((c->mtvec & 1u) && (cause & 0x80000000u))
        c->pc = base + 4u * (cause & 0x7fffffffu);
    else
        c->pc = base;
}

static int take_interrupt(struct rv_cpu *c)
{
    uint32_t p = c->mip & c->mie;
    if (!p || !(c->mstatus & RV_MSTATUS_MIE))
        return 0;
    uint32_t code;
    if (p & (1u << RV_IRQ_MEI)) code = RV_IRQ_MEI;
    else if (p & (1u << RV_IRQ_MSI)) code = RV_IRQ_MSI;
    else if (p & (1u << RV_IRQ_MTI)) code = RV_IRQ_MTI;
    else code = (uint32_t)__builtin_ctz(p);
    trap(c, 0x80000000u | code, 0);
    return 1;
}

/* ---------------------------------------------------------------------- */
/* CSRs                                                                   */
/* ---------------------------------------------------------------------- */

static uint64_t cycle_now(struct rv_cpu *c)
{
    return c->read_cycle ? c->read_cycle(c->counter_ctx) : c->instret;
}

static uint64_t time_now(struct rv_cpu *c)
{
    return c->read_time ? c->read_time(c->counter_ctx) : c->instret / 100;
}

static int csr_read(struct rv_cpu *c, uint32_t csr, uint32_t *v)
{
    switch (csr) {
    case 0x300: *v = c->mstatus; return 0;
    case 0x301: *v = (1u << 30) | (1u << 8) | (1u << 12); return 0; /* RV32 I M */
    case 0x304: *v = c->mie; return 0;
    case 0x305: *v = c->mtvec; return 0;
    case 0x306: *v = 0; return 0;           /* mcounteren */
    case 0x310: *v = 0; return 0;           /* mstatush */
    case 0x320: *v = 0; return 0;           /* mcountinhibit */
    case 0x340: *v = c->mscratch; return 0;
    case 0x341: *v = c->mepc; return 0;
    case 0x342: *v = c->mcause; return 0;
    case 0x343: *v = c->mtval; return 0;
    case 0x344: *v = c->mip; return 0;
    case 0x7a0: case 0x7a1: case 0x7a2: *v = 0; return 0; /* no triggers */
    case 0xb00: case 0xc00: *v = (uint32_t)cycle_now(c); return 0;
    case 0xb80: case 0xc80: *v = (uint32_t)(cycle_now(c) >> 32); return 0;
    case 0xb02: case 0xc02: *v = (uint32_t)c->instret; return 0;
    case 0xb82: case 0xc82: *v = (uint32_t)(c->instret >> 32); return 0;
    case 0xc01: *v = (uint32_t)time_now(c); return 0;
    case 0xc81: *v = (uint32_t)(time_now(c) >> 32); return 0;
    case 0xf11: case 0xf12: case 0xf13: case 0xf14: case 0xf15:
        *v = 0; return 0;                   /* vendor, arch, impl, hart, config */
    default:
        return -1;
    }
}

static int csr_write(struct rv_cpu *c, uint32_t csr, uint32_t v)
{
    if ((csr >> 10) == 3)
        return -1;                          /* read-only CSR */
    switch (csr) {
    case 0x300:
        c->mstatus = (v & (RV_MSTATUS_MIE | RV_MSTATUS_MPIE)) | RV_MSTATUS_MPP;
        return 0;
    case 0x301: return 0;                   /* misa is fixed */
    case 0x304:
        c->mie = v & ((1u << RV_IRQ_MSI) | (1u << RV_IRQ_MTI) | (1u << RV_IRQ_MEI));
        return 0;
    case 0x305: c->mtvec = v & ~2u; return 0;
    case 0x306: case 0x310: case 0x320: return 0;
    case 0x340: c->mscratch = v; return 0;
    case 0x341: c->mepc = v & ~3u; return 0;
    case 0x342: c->mcause = v; return 0;
    case 0x343: c->mtval = v; return 0;
    case 0x344: return 0;                   /* pending bits are driven by the board */
    case 0x7a0: case 0x7a1: case 0x7a2: return 0;
    case 0xb00: case 0xb80: return 0;       /* cycle follows real time */
    /* An explicit write wins over the writing instruction's own increment,
     * which happens right after: store one less. */
    case 0xb02:
        c->instret = ((c->instret & 0xffffffff00000000ull) | v) - 1;
        return 0;
    case 0xb82:
        c->instret = ((c->instret & 0xffffffffull) | ((uint64_t)v << 32)) - 1;
        return 0;
    default:
        return -1;
    }
}

/* ---------------------------------------------------------------------- */
/* Execution                                                              */
/* ---------------------------------------------------------------------- */

#define RD   ((insn >> 7) & 31)
#define RS1  ((insn >> 15) & 31)
#define RS2  ((insn >> 20) & 31)
#define F3   ((insn >> 12) & 7)
#define F7   (insn >> 25)

static inline int32_t imm_i(uint32_t insn) { return (int32_t)insn >> 20; }
static inline int32_t imm_s(uint32_t insn)
{
    return ((int32_t)(insn & 0xfe000000u) >> 20) | (int32_t)((insn >> 7) & 0x1f);
}
static inline int32_t imm_b(uint32_t insn)
{
    return ((int32_t)(insn & 0x80000000u) >> 19)
         | (int32_t)((insn & 0x80u) << 4)
         | (int32_t)((insn >> 20) & 0x7e0u)
         | (int32_t)((insn >> 7) & 0x1eu);
}
static inline int32_t imm_j(uint32_t insn)
{
    return ((int32_t)(insn & 0x80000000u) >> 11)
         | (int32_t)(insn & 0xff000u)
         | (int32_t)((insn >> 9) & 0x800u)
         | (int32_t)((insn >> 20) & 0x7feu);
}

static uint32_t mulh(int32_t a, int32_t b) { return (uint32_t)(((int64_t)a * b) >> 32); }
static uint32_t mulhu(uint32_t a, uint32_t b) { return (uint32_t)(((uint64_t)a * b) >> 32); }
static uint32_t mulhsu(int32_t a, uint32_t b) { return (uint32_t)(((int64_t)a * (int64_t)(uint64_t)b) >> 32); }

uint32_t rv_run(struct rv_cpu *c, uint32_t n)
{
    uint32_t done = 0;
    uint32_t *x = c->x;
    c->stop_batch = 0;

    while (done < n) {
        if (UNLIKELY((c->mip & c->mie) != 0) && take_interrupt(c)) {
            /* fall through and fetch the handler's first instruction */
        }

        uint32_t pc = c->pc;
        uint32_t insn;
        if (UNLIKELY(fetch(c, pc, &insn))) {
            trap(c, RV_EXC_INSN_FAULT, pc);
            done++;
            continue;
        }

        uint32_t next = pc + 4;
        done++;

        switch (insn & 0x7f) {
        case 0x37: /* LUI */
            if (RD) x[RD] = insn & 0xfffff000u;
            break;

        case 0x17: /* AUIPC */
            if (RD) x[RD] = pc + (insn & 0xfffff000u);
            break;

        case 0x6f: { /* JAL */
            uint32_t t = pc + (uint32_t)imm_j(insn);
            if (UNLIKELY(t & 3)) { trap(c, RV_EXC_INSN_MISALIGNED, t); continue; }
            if (RD) x[RD] = next;
            next = t;
            break;
        }

        case 0x67: { /* JALR */
            if (F3 != 0) goto illegal;
            uint32_t t = (x[RS1] + (uint32_t)imm_i(insn)) & ~1u;
            if (UNLIKELY(t & 3)) { trap(c, RV_EXC_INSN_MISALIGNED, t); continue; }
            if (RD) x[RD] = next;
            next = t;
            break;
        }

        case 0x63: { /* BRANCH */
            uint32_t a = x[RS1], b = x[RS2];
            int take;
            switch (F3) {
            case 0: take = a == b; break;
            case 1: take = a != b; break;
            case 4: take = (int32_t)a < (int32_t)b; break;
            case 5: take = (int32_t)a >= (int32_t)b; break;
            case 6: take = a < b; break;
            case 7: take = a >= b; break;
            default: goto illegal;
            }
            if (take) {
                uint32_t t = pc + (uint32_t)imm_b(insn);
                if (UNLIKELY(t & 3)) { trap(c, RV_EXC_INSN_MISALIGNED, t); continue; }
                next = t;
            }
            break;
        }

        case 0x03: { /* LOAD */
            uint32_t a = x[RS1] + (uint32_t)imm_i(insn);
            uint32_t v;
            int size;
            switch (F3) {
            case 0: case 4: size = 1; break;
            case 1: case 5: size = 2; break;
            case 2: size = 4; break;
            default: goto illegal;
            }
            if (UNLIKELY(load(c, a, size, &v))) { trap(c, RV_EXC_LOAD_FAULT, a); continue; }
            if (F3 == 0) v = (uint32_t)(int32_t)(int8_t)v;
            else if (F3 == 1) v = (uint32_t)(int32_t)(int16_t)v;
            if (RD) x[RD] = v;
            break;
        }

        case 0x23: { /* STORE */
            uint32_t a = x[RS1] + (uint32_t)imm_s(insn);
            int size;
            switch (F3) {
            case 0: size = 1; break;
            case 1: size = 2; break;
            case 2: size = 4; break;
            default: goto illegal;
            }
            if (UNLIKELY(store(c, a, size, x[RS2]))) { trap(c, RV_EXC_STORE_FAULT, a); continue; }
            break;
        }

        case 0x13: { /* OP-IMM */
            uint32_t a = x[RS1], r;
            int32_t imm = imm_i(insn);
            switch (F3) {
            case 0: r = a + (uint32_t)imm; break;
            case 2: r = (int32_t)a < imm; break;
            case 3: r = a < (uint32_t)imm; break;
            case 4: r = a ^ (uint32_t)imm; break;
            case 6: r = a | (uint32_t)imm; break;
            case 7: r = a & (uint32_t)imm; break;
            case 1:
                if (F7 != 0) goto illegal;
                r = a << RS2;
                break;
            case 5:
                if (F7 == 0) r = a >> RS2;
                else if (F7 == 0x20) r = (uint32_t)((int32_t)a >> RS2);
                else goto illegal;
                break;
            default: goto illegal;
            }
            if (RD) x[RD] = r;
            break;
        }

        case 0x33: { /* OP */
            uint32_t a = x[RS1], b = x[RS2], r;
            uint32_t f7 = F7;
            if (f7 == 0x01) {
                int32_t sa = (int32_t)a, sb = (int32_t)b;
                switch (F3) {
                case 0: r = a * b; break;
                case 1: r = mulh(sa, sb); break;
                case 2: r = mulhsu(sa, b); break;
                case 3: r = mulhu(a, b); break;
                case 4: /* DIV */
                    if (b == 0) r = 0xffffffffu;
                    else if (sa == INT32_MIN && sb == -1) r = a;
                    else r = (uint32_t)(sa / sb);
                    break;
                case 5: r = b ? a / b : 0xffffffffu; break;
                case 6: /* REM */
                    if (b == 0) r = a;
                    else if (sa == INT32_MIN && sb == -1) r = 0;
                    else r = (uint32_t)(sa % sb);
                    break;
                default: r = b ? a % b : a; break;
                }
            } else if (f7 == 0x00) {
                switch (F3) {
                case 0: r = a + b; break;
                case 1: r = a << (b & 31); break;
                case 2: r = (int32_t)a < (int32_t)b; break;
                case 3: r = a < b; break;
                case 4: r = a ^ b; break;
                case 5: r = a >> (b & 31); break;
                case 6: r = a | b; break;
                default: r = a & b; break;
                }
            } else if (f7 == 0x20) {
                if (F3 == 0) r = a - b;
                else if (F3 == 5) r = (uint32_t)((int32_t)a >> (b & 31));
                else goto illegal;
            } else {
                goto illegal;
            }
            if (RD) x[RD] = r;
            break;
        }

        case 0x0f: /* MISC-MEM: fence, fence.i */
            if (F3 > 1) goto illegal;
            break;

        case 0x73: { /* SYSTEM */
            uint32_t f3 = F3;
            if (f3 == 0) {
                switch (insn) {
                case 0x00000073: trap(c, RV_EXC_ECALL_M, 0); continue;
                case 0x00100073: trap(c, RV_EXC_BREAKPOINT, pc); continue;
                case 0x30200073: /* MRET */
                    next = c->mepc;
                    if (c->mstatus & RV_MSTATUS_MPIE) c->mstatus |= RV_MSTATUS_MIE;
                    else c->mstatus &= ~RV_MSTATUS_MIE;
                    c->mstatus |= RV_MSTATUS_MPIE;
                    break;
                case 0x10500073: /* WFI */
                    c->pc = next;
                    c->instret++;
                    if (!rv_wake_pending(c)) {
                        c->wfi = 1;
                        return done;
                    }
                    continue;
                default:
                    goto illegal;
                }
                break;
            }
            if (f3 == 4) goto illegal;
            uint32_t csr = insn >> 20;
            uint32_t src = (f3 & 4) ? RS1 : x[RS1];   /* zimm or register */
            int writes = ((f3 & 3) == 1) || RS1 != 0;
            uint32_t old = 0;
            if (csr_read(c, csr, &old)) goto illegal;
            if (writes) {
                uint32_t nv;
                switch (f3 & 3) {
                case 1: nv = src; break;
                case 2: nv = old | src; break;
                default: nv = old & ~src; break;
                }
                if (csr_write(c, csr, nv)) goto illegal;
            }
            if (RD) x[RD] = old;
            /* A CSR write can enable a pending interrupt; let the next
             * iteration see it. */
            break;
        }

        default:
        illegal:
            trap(c, RV_EXC_ILLEGAL, insn);
            continue;
        }

        c->pc = next;
        c->instret++;
        if (UNLIKELY(c->stop_batch))
            break;
    }
    return done;
}
