/*
 * Mecrisp Pocket - reads the Mecrisp-Quintus dictionary from guest memory.
 * Copyright (C) 2026 The Mecrisp Pocket authors. GPL-3.0-or-later.
 */
#include "qp/dict.h"

#include <stdlib.h>
#include <string.h>

#define MAX_CHAIN 20000   /* guards against cycles in damaged memory */

struct mem {
    const uint8_t *flash;
    const uint8_t *ram;
};

static int in_flash(uint32_t a, uint32_t len) { return a < QP_FLASH_SIZE && QP_FLASH_SIZE - a >= len; }
static int in_ram(uint32_t a, uint32_t len)
{
    return a - QP_RAM_BASE < QP_RAM_SIZE && QP_RAM_SIZE - (a - QP_RAM_BASE) >= len;
}

static const uint8_t *ptr(const struct mem *m, uint32_t a, uint32_t len)
{
    if (in_flash(a, len)) return m->flash + a;
    if (in_ram(a, len)) return m->ram + (a - QP_RAM_BASE);
    return NULL;
}

static int rd32(const struct mem *m, uint32_t a, uint32_t *v)
{
    const uint8_t *p = ptr(m, a, 4);
    if (!p) return 0;
    memcpy(v, p, 4);
    return 1;
}

static int rd8(const struct mem *m, uint32_t a, uint8_t *v)
{
    const uint8_t *p = ptr(m, a, 1);
    if (!p) return 0;
    *v = *p;
    return 1;
}

/* Decodes the entry at header a. Returns 0 if it is not readable. */
static int decode(const struct mem *m, uint32_t a, qp_word *w)
{
    uint32_t flags;
    uint8_t len;
    if ((a & 3) || !rd32(m, a + 4, &flags) || !rd8(m, a + 8, &len)) return 0;
    const uint8_t *name = ptr(m, a + 9, len);
    if (!name) return 0;
    uint32_t n = len > QP_WORD_NAME_MAX ? QP_WORD_NAME_MAX : len;
    memcpy(w->name, name, n);
    w->name[n] = 0;
    w->header = a;
    w->code = (a + 9 + len + 3) & ~3u;
    w->flags = flags;
    w->in_ram = in_ram(a, 1) ? 1 : 0;
    return 1;
}

struct list_job {
    const qp_dict_layout *layout;
    qp_word *out;
    int max;
    int count;
};

static void add(struct list_job *job, const qp_word *w)
{
    if (w->flags == QP_FLAG_INVISIBLE || w->name[0] == 0) return;
    if (job->count < job->max) job->out[job->count] = *w;
    job->count++;
}

static void walk(const struct mem *m, struct list_job *job)
{
    const qp_dict_layout *l = job->layout;
    uint32_t dp, start;
    if (!rd32(m, l->dictionary_ptr, &dp)) return;
    /* While compiling into flash, Forth does not see RAM definitions
     * (and keeps the RAM chain's latest in the second pointer): skip them. */
    if (!in_ram(dp, 1)) start = 0;
    else if (!rd32(m, l->latest, &start)) return;

    /* RAM chain, newest to oldest, until it reaches flash. */
    qp_word w;
    uint32_t a = start;
    for (int guard = 0; in_ram(a, 12) && guard < MAX_CHAIN; guard++) {
        if (!decode(m, a, &w)) break;
        add(job, &w);
        uint32_t link;
        if (!rd32(m, a, &link) || link == a) break;
        a = link;
    }

    /* Flash chain, oldest to newest. Collected first, then added newest first. */
    uint32_t *chain = malloc(MAX_CHAIN * sizeof *chain);
    if (!chain) return;
    int n = 0;
    a = l->core_dictionary;
    while (n < MAX_CHAIN && in_flash(a, 12)) {
        chain[n++] = a;
        uint32_t link;
        uint8_t next_len;
        if (!rd32(m, a, &link) || link == 0xffffffffu || link <= a) break;
        if (!rd8(m, link + 8, &next_len) || next_len == 0xff) break;
        a = link;
    }
    for (int i = n - 1; i >= 0; i--)
        if (decode(m, chain[i], &w)) add(job, &w);
    free(chain);
}

int qp_dict_word_at(const qp_word *words, int count, uint32_t addr)
{
    int best = -1;
    for (int i = 0; i < count; i++) {
        if (words[i].code > addr) continue;
        if (best < 0 || words[i].code > words[best].code) best = i;
    }
    if (best < 0) return -1;
    /* Not inside the word if another header starts between its code and addr. */
    for (int i = 0; i < count; i++)
        if (words[i].header > words[best].code && words[i].header <= addr) return -1;
    return best;
}

/* qp_dict_list runs the walk on the VM thread through qp_with_memory. */
static void list_fn(const uint8_t *flash, const uint8_t *ram, void *arg)
{
    struct mem m = { flash, ram };
    walk(&m, arg);
}

int qp_dict_list(qp_vm *vm, const qp_dict_layout *layout, qp_word *out, int max)
{
    struct list_job job = { layout, out, max, 0 };
    qp_with_memory(vm, list_fn, &job);
    return job.count;
}
