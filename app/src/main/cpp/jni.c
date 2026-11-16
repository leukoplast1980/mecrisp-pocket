/*
 * Mecrisp Pocket - JNI bridge between NativeVm.kt and the board emulator.
 * Copyright (C) 2026 The Mecrisp Pocket authors. GPL-3.0-or-later.
 *
 * Handles are qp_vm pointers stored in a Java long.
 */
#include <jni.h>
#include <stdlib.h>
#include <string.h>

#include "qp/dict.h"
#include "qp/pocket.h"

#define FN(name) Java_io_github_leukoplast1980_mecrisppocket_vm_NativeVm_##name

static qp_vm *VM(jlong h) { return (qp_vm *)(intptr_t)h; }

JNIEXPORT jlong JNICALL FN(create)(JNIEnv *env, jclass cls, jboolean virtual_time)
{
    (void)env; (void)cls;
    qp_config cfg = { 0 };
    cfg.virtual_time = virtual_time ? 1 : 0;
    return (jlong)(intptr_t)qp_create(&cfg);
}

JNIEXPORT void JNICALL FN(destroy)(JNIEnv *env, jclass cls, jlong h)
{
    (void)env; (void)cls;
    qp_destroy(VM(h));
}

JNIEXPORT void JNICALL FN(loadFlash)(JNIEnv *env, jclass cls, jlong h, jbyteArray image)
{
    (void)cls;
    jsize len = (*env)->GetArrayLength(env, image);
    jbyte *data = (*env)->GetByteArrayElements(env, image, NULL);
    if (!data) return;
    qp_load_flash(VM(h), (const uint8_t *)data, (size_t)len);
    (*env)->ReleaseByteArrayElements(env, image, data, JNI_ABORT);
}

/* Returns the flash image if it changed since the last call, else null. */
JNIEXPORT jbyteArray JNICALL FN(copyFlashIfDirty)(JNIEnv *env, jclass cls, jlong h)
{
    (void)cls;
    if (!qp_flash_dirty(VM(h))) return NULL;
    uint8_t *buf = malloc(QP_FLASH_SIZE);
    if (!buf) return NULL;
    qp_copy_flash(VM(h), buf);
    jbyteArray out = (*env)->NewByteArray(env, QP_FLASH_SIZE);
    if (out) (*env)->SetByteArrayRegion(env, out, 0, QP_FLASH_SIZE, (const jbyte *)buf);
    free(buf);
    return out;
}

/* Blocks: runs guest code on the calling thread until shutdown(). */
JNIEXPORT void JNICALL FN(run)(JNIEnv *env, jclass cls, jlong h)
{
    (void)env; (void)cls;
    qp_run(VM(h));
}

JNIEXPORT void JNICALL FN(shutdown)(JNIEnv *env, jclass cls, jlong h)
{
    (void)env; (void)cls;
    qp_shutdown(VM(h));
}

JNIEXPORT void JNICALL FN(pause)(JNIEnv *env, jclass cls, jlong h, jboolean paused)
{
    (void)env; (void)cls;
    qp_pause(VM(h), paused ? 1 : 0);
}

JNIEXPORT void JNICALL FN(step)(JNIEnv *env, jclass cls, jlong h)
{
    (void)env; (void)cls;
    qp_step(VM(h));
}

JNIEXPORT void JNICALL FN(requestBreak)(JNIEnv *env, jclass cls, jlong h)
{
    (void)env; (void)cls;
    qp_request_break(VM(h));
}

JNIEXPORT void JNICALL FN(requestReset)(JNIEnv *env, jclass cls, jlong h, jboolean safe_boot)
{
    (void)env; (void)cls;
    qp_request_reset(VM(h), safe_boot ? 1 : 0);
}

JNIEXPORT jint JNICALL FN(uartWrite)(JNIEnv *env, jclass cls, jlong h,
                                     jbyteArray data, jint off, jint len)
{
    (void)cls;
    jbyte *p = (*env)->GetByteArrayElements(env, data, NULL);
    if (!p) return 0;
    size_t n = qp_uart_write(VM(h), (const uint8_t *)p + off, (size_t)len);
    (*env)->ReleaseByteArrayElements(env, data, p, JNI_ABORT);
    return (jint)n;
}

JNIEXPORT jint JNICALL FN(uartRead)(JNIEnv *env, jclass cls, jlong h, jbyteArray buf)
{
    (void)cls;
    jsize cap = (*env)->GetArrayLength(env, buf);
    uint8_t tmp[4096];
    if (cap > (jsize)sizeof tmp) cap = (jsize)sizeof tmp;
    size_t n = qp_uart_read(VM(h), tmp, (size_t)cap);
    if (n) (*env)->SetByteArrayRegion(env, buf, 0, (jsize)n, (const jbyte *)tmp);
    return (jint)n;
}

JNIEXPORT jboolean JNICALL FN(waitingForKey)(JNIEnv *env, jclass cls, jlong h)
{
    (void)env; (void)cls;
    return qp_waiting_for_key(VM(h)) ? JNI_TRUE : JNI_FALSE;
}

/* Snapshot as ints: pc, x0..x31, mstatus, mie, mip, mtvec, mepc, mcause,
 * mtval, mscratch, instret lo/hi, mtime lo/hi, waiting-for-key, paused. */
#define SNAP_INTS 47

JNIEXPORT jintArray JNICALL FN(snapshot)(JNIEnv *env, jclass cls, jlong h)
{
    (void)cls;
    qp_snapshot s;
    qp_snapshot_get(VM(h), &s);
    jint v[SNAP_INTS];
    int i = 0;
    v[i++] = (jint)s.pc;
    for (int r = 0; r < 32; r++) v[i++] = (jint)s.x[r];
    v[i++] = (jint)s.mstatus;
    v[i++] = (jint)s.mie;
    v[i++] = (jint)s.mip;
    v[i++] = (jint)s.mtvec;
    v[i++] = (jint)s.mepc;
    v[i++] = (jint)s.mcause;
    v[i++] = (jint)s.mtval;
    v[i++] = (jint)s.mscratch;
    v[i++] = (jint)(uint32_t)s.instret;
    v[i++] = (jint)(uint32_t)(s.instret >> 32);
    v[i++] = (jint)(uint32_t)s.mtime;
    v[i++] = (jint)(uint32_t)(s.mtime >> 32);
    v[i++] = s.waiting_for_key;
    v[i++] = s.paused;
    jintArray out = (*env)->NewIntArray(env, SNAP_INTS);
    if (out) (*env)->SetIntArrayRegion(env, out, 0, SNAP_INTS, v);
    return out;
}

JNIEXPORT jintArray JNICALL FN(deviceRegs)(JNIEnv *env, jclass cls, jlong h, jintArray addrs)
{
    (void)cls;
    jsize n = (*env)->GetArrayLength(env, addrs);
    uint32_t *a = malloc(sizeof *a * (size_t)(n ? n : 1));
    uint32_t *v = malloc(sizeof *v * (size_t)(n ? n : 1));
    jintArray out = NULL;
    if (a && v) {
        (*env)->GetIntArrayRegion(env, addrs, 0, n, (jint *)a);
        qp_get_device_regs(VM(h), a, v, (int)n);
        out = (*env)->NewIntArray(env, n);
        if (out) (*env)->SetIntArrayRegion(env, out, 0, n, (const jint *)v);
    }
    free(a);
    free(v);
    return out;
}

JNIEXPORT jbyteArray JNICALL FN(readMemory)(JNIEnv *env, jclass cls, jlong h, jint addr, jint len)
{
    (void)cls;
    if (len <= 0 || len > (1 << 20)) return NULL;
    uint8_t *buf = malloc((size_t)len);
    if (!buf) return NULL;
    qp_read_memory(VM(h), (uint32_t)addr, buf, (size_t)len);
    jbyteArray out = (*env)->NewByteArray(env, len);
    if (out) (*env)->SetByteArrayRegion(env, out, 0, len, (const jbyte *)buf);
    free(buf);
    return out;
}

/* Returns { byte[] names (concatenated UTF-8), int[] nameLengths,
 *           int[] headers, int[] codes, int[] flags } in search order.
 * layout = { CoreDictionaryAnfang, Dictionarypointer, Fadenende, ZweitFadenende } */
JNIEXPORT jobjectArray JNICALL FN(dictionary)(JNIEnv *env, jclass cls, jlong h, jintArray layout)
{
    (void)cls;
    jint l[4];
    if ((*env)->GetArrayLength(env, layout) != 4) return NULL;
    (*env)->GetIntArrayRegion(env, layout, 0, 4, l);
    qp_dict_layout lay = { (uint32_t)l[0], (uint32_t)l[1], (uint32_t)l[2], (uint32_t)l[3] };

    int max = 8192;
    qp_word *w = malloc(sizeof *w * (size_t)max);
    if (!w) return NULL;
    int n = qp_dict_list(VM(h), &lay, w, max);
    if (n > max) n = max;

    size_t total = 0;
    for (int i = 0; i < n; i++) total += strlen(w[i].name);
    jbyte *names = malloc(total ? total : 1);
    jint *lens = malloc(sizeof *lens * (size_t)(n ? n : 1));
    jint *headers = malloc(sizeof *headers * (size_t)(n ? n : 1));
    jint *codes = malloc(sizeof *codes * (size_t)(n ? n : 1));
    jint *flags = malloc(sizeof *flags * (size_t)(n ? n : 1));
    jobjectArray out = NULL;
    if (names && lens && headers && codes && flags) {
        size_t off = 0;
        for (int i = 0; i < n; i++) {
            size_t len = strlen(w[i].name);
            memcpy(names + off, w[i].name, len);
            off += len;
            lens[i] = (jint)len;
            headers[i] = (jint)w[i].header;
            codes[i] = (jint)w[i].code;
            flags[i] = (jint)w[i].flags;
        }
        jclass objcls = (*env)->FindClass(env, "java/lang/Object");
        out = (*env)->NewObjectArray(env, 5, objcls, NULL);
        if (out) {
            jbyteArray jn = (*env)->NewByteArray(env, (jsize)total);
            (*env)->SetByteArrayRegion(env, jn, 0, (jsize)total, names);
            (*env)->SetObjectArrayElement(env, out, 0, jn);
            jint *arrays[4] = { lens, headers, codes, flags };
            for (int k = 0; k < 4; k++) {
                jintArray ja = (*env)->NewIntArray(env, n);
                (*env)->SetIntArrayRegion(env, ja, 0, n, arrays[k]);
                (*env)->SetObjectArrayElement(env, out, k + 1, ja);
            }
        }
    }
    free(names); free(lens); free(headers); free(codes); free(flags);
    free(w);
    return out;
}

/* Sends one line and waits for Forth: 0 ok, 1 error, 2 timeout. Blocks:
 * call off the UI thread. */
JNIEXPORT jint JNICALL FN(sendLine)(JNIEnv *env, jclass cls, jlong h, jbyteArray line, jint timeout_ms)
{
    (void)cls;
    jsize len = (*env)->GetArrayLength(env, line);
    jbyte *p = (*env)->GetByteArrayElements(env, line, NULL);
    if (!p) return QP_LINE_ERROR;
    int r = qp_send_line(VM(h), (const char *)p, (size_t)len, timeout_ms);
    (*env)->ReleaseByteArrayElements(env, line, p, JNI_ABORT);
    return r;
}

/* Phone devices ----------------------------------------------------------- */

/* Fills out with up to out.length / 5 events (type, a, b, c, d each);
 * returns how many. */
JNIEXPORT jint JNICALL FN(pollEvents)(JNIEnv *env, jclass cls, jlong h, jintArray out)
{
    (void)cls;
    jsize len = (*env)->GetArrayLength(env, out);
    size_t max = (size_t)len / 5;
    if (max == 0) return 0;
    qp_event ev[64];
    if (max > 64) max = 64;
    size_t n = qp_poll_events(VM(h), ev, max);
    if (n == 0) return 0;
    jint vals[64 * 5];
    for (size_t i = 0; i < n; i++) {
        vals[i * 5 + 0] = ev[i].type;
        vals[i * 5 + 1] = ev[i].a;
        vals[i * 5 + 2] = ev[i].b;
        vals[i * 5 + 3] = ev[i].c;
        vals[i * 5 + 4] = ev[i].d;
    }
    (*env)->SetIntArrayRegion(env, out, 0, (jsize)(n * 5), vals);
    return (jint)n;
}

/* GPIO -------------------------------------------------------------------- */

/* out: dir, out, ext, levels, now_us, changes (6 longs). */
JNIEXPORT void JNICALL FN(gpioState)(JNIEnv *env, jclass cls, jlong h, jlongArray out)
{
    (void)cls;
    qp_gpio_state st;
    qp_gpio_get(VM(h), &st);
    jlong vals[6] = { st.dir, st.out, st.ext, st.levels, (jlong)st.now_us, (jlong)st.changes };
    (*env)->SetLongArrayRegion(env, out, 0, 6, vals);
}

JNIEXPORT void JNICALL FN(gpioSetInput)(JNIEnv *env, jclass cls, jlong h, jint mask, jint levels)
{
    (void)env; (void)cls;
    qp_gpio_set_input(VM(h), (uint32_t)mask, (uint32_t)levels);
}

/* Trace entries from index `from` on. out[0] = index of the first entry
 * returned, then two longs per entry: time in us, and
 * levels | toggled << 16 | busy << 32. Returns the number of entries. */
JNIEXPORT jint JNICALL FN(gpioTrace)(JNIEnv *env, jclass cls, jlong h, jlong from, jlongArray out)
{
    (void)cls;
    jsize len = (*env)->GetArrayLength(env, out);
    if (len < 3) return 0;
    size_t max = (size_t)(len - 1) / 2;
    qp_gpio_change *ch = malloc(max * sizeof *ch);
    jlong *vals = malloc((size_t)len * sizeof *vals);
    if (!ch || !vals) { free(ch); free(vals); return 0; }
    uint64_t first = 0;
    size_t n = qp_gpio_trace(VM(h), from < 0 ? 0 : (uint64_t)from, ch, max, &first);
    vals[0] = (jlong)first;
    for (size_t i = 0; i < n; i++) {
        vals[1 + i * 2] = (jlong)ch[i].t_us;
        vals[2 + i * 2] = (jlong)ch[i].levels | ((jlong)ch[i].toggled << 16) | ((jlong)ch[i].busy << 32);
    }
    (*env)->SetLongArrayRegion(env, out, 0, (jsize)(1 + n * 2), vals);
    free(ch);
    free(vals);
    return (jint)n;
}
