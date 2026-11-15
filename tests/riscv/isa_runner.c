/*
 * Runs one riscv-tests ISA test (ELF32, "p" environment) on the Mecrisp
 * Pocket RV32IM core with plain RAM at the ELF's load address.
 * Copyright (C) 2026 The Mecrisp Pocket authors. GPL-3.0-or-later.
 *
 * Usage: isa-runner TEST.elf    Exit status 0 = pass.
 */
#include "rv32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAM_BASE 0x80000000u
#define RAM_SIZE (4u << 20)

static uint8_t ram[RAM_SIZE];

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: isa-runner TEST.elf\n"); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *elf = malloc((size_t)sz);
    if (fread(elf, 1, (size_t)sz, f) != (size_t)sz) { perror("read"); return 1; }
    fclose(f);

    if (memcmp(elf, "\177ELF", 4) || elf[4] != 1) { fprintf(stderr, "not ELF32\n"); return 1; }
    uint32_t entry = rd32(elf + 24);
    uint32_t phoff = rd32(elf + 28), shoff = rd32(elf + 32);
    uint16_t phentsize = rd16(elf + 42), phnum = rd16(elf + 44);
    uint16_t shentsize = rd16(elf + 46), shnum = rd16(elf + 48);

    for (int i = 0; i < phnum; i++) {
        const uint8_t *ph = elf + phoff + (uint32_t)i * phentsize;
        if (rd32(ph) != 1) continue;  /* PT_LOAD */
        uint32_t off = rd32(ph + 4), paddr = rd32(ph + 12);
        uint32_t filesz = rd32(ph + 16), memsz = rd32(ph + 20);
        if (paddr < RAM_BASE || paddr - RAM_BASE + memsz > RAM_SIZE) {
            fprintf(stderr, "segment outside RAM\n");
            return 1;
        }
        memcpy(ram + (paddr - RAM_BASE), elf + off, filesz);
    }

    /* Find the tohost symbol. */
    uint32_t tohost = 0;
    for (int i = 0; i < shnum; i++) {
        const uint8_t *sh = elf + shoff + (uint32_t)i * shentsize;
        if (rd32(sh + 4) != 2) continue;  /* SHT_SYMTAB */
        uint32_t symoff = rd32(sh + 16), symsize = rd32(sh + 20), link = rd32(sh + 24);
        const uint8_t *strsh = elf + shoff + link * shentsize;
        const char *strtab = (const char *)elf + rd32(strsh + 16);
        for (uint32_t s = 0; s < symsize; s += 16) {
            const uint8_t *sym = elf + symoff + s;
            if (!strcmp(strtab + rd32(sym), "tohost")) tohost = rd32(sym + 4);
        }
    }
    if (!tohost) { fprintf(stderr, "no tohost symbol\n"); return 1; }

    struct rv_bus bus = { 0 };
    bus.region[0] = (struct rv_region){ .base = RAM_BASE, .size = RAM_SIZE, .mem = ram };
    bus.nregions = 1;
    struct rv_cpu cpu = { 0 };
    cpu.bus = &bus;
    rv_reset(&cpu, entry);

    for (uint64_t total = 0; total < 50000000ull; ) {
        total += rv_run(&cpu, 1000);
        if (cpu.wfi) cpu.wfi = 0;
        uint32_t v = rd32(ram + (tohost - RAM_BASE));
        if (v) {
            if (v == 1) return 0;
            fprintf(stderr, "%s: FAIL test %u\n", argv[1], v >> 1);
            return 1;
        }
    }
    fprintf(stderr, "%s: timeout (pc=%08x)\n", argv[1], cpu.pc);
    return 1;
}
