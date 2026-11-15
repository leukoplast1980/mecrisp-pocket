/*
 * Mecrisp Pocket - JNI bridge between NativeVm.kt and the board emulator.
 * Copyright (C) 2026 The Mecrisp Pocket authors. GPL-3.0-or-later.
 *
 * Handles are qp_vm pointers stored in a Java long.
 */
#include <jni.h>
#include <stdlib.h>

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
