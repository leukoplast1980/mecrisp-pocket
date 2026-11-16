/*
 * qpemu - Mecrisp Pocket board emulator for Linux.
 * Copyright (C) 2026 The Mecrisp Pocket authors. GPL-3.0-or-later.
 *
 * Usage: qpemu [options] IMAGE [actions...]
 *
 * Options:
 *   --flash FILE      persistent flash: loaded instead of IMAGE if FILE exists,
 *                     written back on exit if flash changed
 *   --virtual-time    counters follow the instruction count (reproducible)
 *   --timeout MS      how long to wait for Forth to become idle (default 5000)
 *   --stats           print instruction count and VM time on exit
 *   --syms FILE       core symbol table (nm output), needed by --words
 *
 * Actions, run in order once Forth is idle at the prompt:
 *   -e TEXT           send TEXT as one line, wait until idle
 *   -s TEXT           send TEXT as one line, don't wait (e.g. an endless loop)
 *   --sleep MS        wait MS milliseconds
 *   -f FILE           send FILE line by line, each line must end with " ok.";
 *                     stops at the first error (exit status 3)
 *   --break-after MS  wait MS milliseconds, press Stop, wait until idle
 *   --reset           press Reset, wait until idle
 *   --safe-reset      long-press Reset (safe boot), wait until idle
 *   --words           print the dictionary as read by the C dictionary reader
 *   --where           print pc and the word it is in
 *   -i                interactive: stdin goes to the terminal until EOF
 *
 * With no actions, qpemu runs interactively. Exit status is 0, 2 if Forth
 * did not become idle within the timeout, or 3 if a -f file had an error.
 */
#define _POSIX_C_SOURCE 200809L
#include "qp/dict.h"
#include "qp/pocket.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static qp_vm *vm;
static qp_dict_layout layout;
static int have_layout;
static atomic_int output_done;
static int timeout_ms = 5000;

static void *vm_thread(void *arg)
{
    (void)arg;
    qp_run(vm);
    return NULL;
}

static void *output_thread(void *arg)
{
    (void)arg;
    uint8_t buf[4096];
    while (!atomic_load(&output_done)) {
        size_t n = qp_uart_read_wait(vm, buf, sizeof buf, 50);
        if (n) {
            fwrite(buf, 1, n, stdout);
            fflush(stdout);
        }
    }
    size_t n;
    while ((n = qp_uart_read(vm, buf, sizeof buf)) > 0)
        fwrite(buf, 1, n, stdout);
    fflush(stdout);
    return NULL;
}

static void sleep_ms(int ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static int wait_idle(void)
{
    /* Give the VM a moment to pick up input before testing for idle. */
    sleep_ms(2);
    if (qp_wait_idle(vm, timeout_ms)) return 0;
    fprintf(stderr, "\nqpemu: Forth did not become idle within %d ms\n", timeout_ms);
    return 2;
}

static void send_all(const char *s, size_t len)
{
    while (len) {
        size_t n = qp_uart_write(vm, (const uint8_t *)s, len);
        s += n;
        len -= n;
        if (len) sleep_ms(1);
    }
}

static int send_line(const char *line)
{
    send_all(line, strlen(line));
    send_all("\n", 1);
    return wait_idle();
}

/* Loads a file line by line, stopping at the first line Forth rejects. */
static int send_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return 1; }
    char line[1024];
    int rc = 0, number = 0;
    while (!rc && fgets(line, sizeof line, f)) {
        number++;
        line[strcspn(line, "\r\n")] = 0;
        int r = qp_send_line(vm, line, strlen(line), timeout_ms);
        if (r == QP_LINE_ERROR) {
            sleep_ms(20);
            fprintf(stderr, "\nqpemu: %s:%d: error, stopped\n", path, number);
            rc = 3;
        } else if (r == QP_LINE_TIMEOUT) {
            sleep_ms(20);
            fprintf(stderr, "\nqpemu: %s:%d: still running after %d ms, stopped\n", path, number, timeout_ms);
            rc = 2;
        }
    }
    fclose(f);
    return rc;
}

static void interactive(void)
{
    char buf[256];
    ssize_t n;
    while ((n = read(0, buf, sizeof buf)) > 0)
        send_all(buf, (size_t)n);
    wait_idle();
}

/* Reads the dictionary addresses from an nm listing (core.syms). */
static int read_syms(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return 0; }
    char line[256], type[8], name[200];
    unsigned addr;
    int found = 0;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "%x %7s %199s", &addr, type, name) != 3) continue;
        if (!strcmp(name, "CoreDictionaryAnfang")) { layout.core_dictionary = addr; found |= 1; }
        else if (!strcmp(name, "Dictionarypointer")) { layout.dictionary_ptr = addr; found |= 2; }
        else if (!strcmp(name, "Fadenende")) { layout.latest = addr; found |= 4; }
        else if (!strcmp(name, "ZweitFadenende")) { layout.latest2 = addr; found |= 8; }
    }
    fclose(f);
    if (found != 15) { fprintf(stderr, "%s: dictionary symbols missing\n", path); return 0; }
    return 1;
}

/* Prints the dictionary as seen by the C reader, one "word: NAME" per line. */
static int print_words(void)
{
    if (!have_layout) { fprintf(stderr, "qpemu: --words needs --syms\n"); return 1; }
    int max = 4096;
    qp_word *w = malloc(sizeof *w * (size_t)max);
    int n = qp_dict_list(vm, &layout, w, max);
    sleep_ms(20);  /* let pending Forth output go first */
    for (int k = 0; k < n && k < max; k++)
        printf("word: %s %08x %08x%s\n", w[k].name, w[k].code, w[k].flags, w[k].in_ram ? " ram" : "");
    fflush(stdout);
    free(w);
    return 0;
}

/* Prints the device events the board sent since the last call, one per line:
 * "event: vibrate MS", "event: brightness N", "event: tone HZ MS WAVE VOLUME". */
static int print_events(void)
{
    qp_event ev[64];
    size_t n;
    sleep_ms(20);  /* let pending Forth output go first */
    while ((n = qp_poll_events(vm, ev, 64)) > 0) {
        for (size_t k = 0; k < n; k++) {
            switch (ev[k].type) {
            case QP_EVENT_VIBRATE: printf("event: vibrate %d\n", ev[k].a); break;
            case QP_EVENT_BRIGHTNESS: printf("event: brightness %d\n", ev[k].a); break;
            case QP_EVENT_TONE:
                printf("event: tone %d %d %d %d\n", ev[k].a, ev[k].b, ev[k].c, ev[k].d);
                break;
            default: printf("event: %d\n", ev[k].type); break;
            }
        }
    }
    fflush(stdout);
    return 0;
}

/* Prints the GPIO state and the trace:
 * "gpio: dir DIR out OUT ext EXT levels LEVELS" (hex), then
 * "change: LEVELS TOGGLED BUSY" (hex) for each trace entry, oldest first. */
static int print_gpio(void)
{
    qp_gpio_state st;
    qp_gpio_get(vm, &st);
    static qp_gpio_change ch[QP_GPIO_TRACE_SIZE];
    uint64_t first;
    size_t n = qp_gpio_trace(vm, 0, ch, QP_GPIO_TRACE_SIZE, &first);
    sleep_ms(20);
    printf("gpio: dir %04x out %04x ext %04x levels %04x\n", st.dir, st.out, st.ext, st.levels);
    for (size_t k = 0; k < n; k++)
        printf("change: %04x %04x %04x\n", ch[k].levels, ch[k].toggled, ch[k].busy);
    fflush(stdout);
    return 0;
}

/* Prints where the CPU is: pc and the word containing it. */
static int print_where(void)
{
    if (!have_layout) { fprintf(stderr, "qpemu: --where needs --syms\n"); return 1; }
    qp_snapshot snap;
    qp_snapshot_get(vm, &snap);
    int max = 4096;
    qp_word *w = malloc(sizeof *w * (size_t)max);
    int n = qp_dict_list(vm, &layout, w, max);
    int i = qp_dict_word_at(w, n < max ? n : max, snap.pc);
    sleep_ms(20);
    if (i >= 0) printf("where: pc %08x in %s +%u\n", snap.pc, w[i].name, snap.pc - w[i].code);
    else printf("where: pc %08x\n", snap.pc);
    fflush(stdout);
    free(w);
    return 0;
}

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *data = malloc(QP_FLASH_SIZE);
    *len = fread(data, 1, QP_FLASH_SIZE, f);
    fclose(f);
    return data;
}

static void usage(void)
{
    fprintf(stderr, "usage: qpemu [--flash FILE] [--virtual-time] [--timeout MS] IMAGE"
                    " [-e TEXT | -f FILE | --break-after MS | --reset | --safe-reset"
                    " | --events | --gpio | --gpio-in LEVELS | -i]...\n");
    exit(1);
}

int main(int argc, char **argv)
{
    const char *flash_path = NULL;
    int stats = 0;
    qp_config cfg = { 0 };
    int i = 1;

    for (; i < argc && argv[i][0] == '-' && argv[i][1] == '-'; i++) {
        if (!strcmp(argv[i], "--flash") && i + 1 < argc) flash_path = argv[++i];
        else if (!strcmp(argv[i], "--virtual-time")) cfg.virtual_time = 1;
        else if (!strcmp(argv[i], "--stats")) stats = 1;
        else if (!strcmp(argv[i], "--syms") && i + 1 < argc) {
            if (!read_syms(argv[++i])) return 1;
            have_layout = 1;
        }
        else if (!strcmp(argv[i], "--timeout") && i + 1 < argc) timeout_ms = atoi(argv[++i]);
        else usage();
    }
    if (i >= argc) usage();
    const char *image_path = argv[i++];

    size_t len = 0;
    uint8_t *image = NULL;
    if (flash_path) image = read_file(flash_path, &len);
    if (!image) image = read_file(image_path, &len);
    if (!image) { perror(image_path); return 1; }

    vm = qp_create(&cfg);
    qp_load_flash(vm, image, len);
    free(image);

    pthread_t vt, ot;
    pthread_create(&vt, NULL, vm_thread, NULL);
    pthread_create(&ot, NULL, output_thread, NULL);

    /* A hanging init word is not fatal here: a --safe-reset may follow. */
    wait_idle();
    int rc = 0;
    int any_action = i < argc;

    for (; !rc && i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-e") && i + 1 < argc) rc = send_line(argv[++i]);
        else if (!strcmp(a, "-s") && i + 1 < argc) {
            const char *t = argv[++i];
            send_all(t, strlen(t));
            send_all("\n", 1);
        }
        else if (!strcmp(a, "--sleep") && i + 1 < argc) sleep_ms(atoi(argv[++i]));
        else if (!strcmp(a, "-f") && i + 1 < argc) rc = send_file(argv[++i]);
        else if (!strcmp(a, "--break-after") && i + 1 < argc) {
            sleep_ms(atoi(argv[++i]));
            qp_request_break(vm);
            rc = wait_idle();
        } else if (!strcmp(a, "--reset")) {
            qp_request_reset(vm, 0);
            rc = wait_idle();
        } else if (!strcmp(a, "--safe-reset")) {
            qp_request_reset(vm, 1);
            rc = wait_idle();
        } else if (!strcmp(a, "--where")) {
            rc = print_where();
        } else if (!strcmp(a, "--words")) {
            rc = print_words();
        } else if (!strcmp(a, "--events")) {
            rc = print_events();
        } else if (!strcmp(a, "--gpio")) {
            rc = print_gpio();
        } else if (!strcmp(a, "--gpio-in") && i + 1 < argc) {
            qp_gpio_set_input(vm, QP_GPIO_MASK, (uint32_t)strtoul(argv[++i], NULL, 0));
        } else if (!strcmp(a, "-i")) {
            interactive();
        } else {
            usage();
        }
    }
    if (!any_action && !rc) interactive();

    sleep_ms(20);
    atomic_store(&output_done, 1);
    pthread_join(ot, NULL);

    if (flash_path) {
        uint8_t *buf = malloc(QP_FLASH_SIZE);
        if (qp_copy_flash(vm, buf)) {
            FILE *f = fopen(flash_path, "wb");
            if (!f || fwrite(buf, 1, QP_FLASH_SIZE, f) != QP_FLASH_SIZE) {
                perror(flash_path);
                rc = rc ? rc : 1;
            }
            if (f) fclose(f);
        }
        free(buf);
    }

    if (stats) {
        qp_snapshot snap;
        qp_snapshot_get(vm, &snap);
        fprintf(stderr, "qpemu: %llu instructions in %.3f s of VM time\n",
                (unsigned long long)snap.instret, snap.mtime / 1e6);
    }

    qp_shutdown(vm);
    pthread_join(vt, NULL);
    qp_destroy(vm);
    return rc;
}
