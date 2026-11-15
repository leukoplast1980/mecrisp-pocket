/*
 * Mecrisp Pocket - reads the Mecrisp-Quintus dictionary from guest memory.
 * Copyright (C) 2026 The Mecrisp Pocket authors. GPL-3.0-or-later.
 *
 * Dictionary entry (all targets without compressed instructions):
 *   4 bytes link, 4 bytes flags, 1 byte name length, name, code aligned on 4.
 * The RAM chain runs from the newest definition to the oldest; the oldest
 * RAM definition links to the first core definition. From there the flash
 * chain runs from the oldest definition to the newest, through the core and
 * into the user flash dictionary. While compiling into flash, Forth does not
 * see RAM definitions, and neither does this reader.
 */
#ifndef QP_DICT_H
#define QP_DICT_H

#include <stdint.h>

#include "qp/pocket.h"

#ifdef __cplusplus
extern "C" {
#endif

#define QP_WORD_NAME_MAX 64

/* Addresses from the core's symbol table (pocket.json). */
typedef struct {
    uint32_t core_dictionary;  /* CoreDictionaryAnfang */
    uint32_t dictionary_ptr;   /* Dictionarypointer variable */
    uint32_t latest;           /* Fadenende variable */
    uint32_t latest2;          /* ZweitFadenende variable */
} qp_dict_layout;

/* Flag bits (visible definitions; invisible ones are 0xFFFFFFFF). */
#define QP_FLAG_INVISIBLE  0xffffffffu
#define QP_FLAG_IMMEDIATE  0x10u
#define QP_FLAG_INLINE     0x20u
#define QP_FLAG_FOLDABLE   0x40u
#define QP_FLAG_RAMALLOT   0x80u   /* variable, 2variable, buffer: */

typedef struct {
    uint32_t header;  /* address of the link field */
    uint32_t code;    /* address of the first instruction */
    uint32_t flags;
    uint8_t in_ram;
    char name[QP_WORD_NAME_MAX + 1];
} qp_word;

/* Lists the words Forth can currently find: RAM newest first (only in
 * compiletoram mode), then flash newest first. Invisible entries are skipped. Returns the number of words found (may exceed max; only
 * max entries are written). */
int qp_dict_list(qp_vm *vm, const qp_dict_layout *layout, qp_word *out, int max);

/* Finds the word whose code contains addr, using a list from qp_dict_list.
 * Returns its index, or -1. */
int qp_dict_word_at(const qp_word *words, int count, uint32_t addr);

#ifdef __cplusplus
}
#endif

#endif
