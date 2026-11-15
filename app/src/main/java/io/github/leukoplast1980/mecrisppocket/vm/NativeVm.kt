/*
 * Mecrisp Pocket - JNI entry points of the board emulator (see jni.c).
 * Copyright (C) 2026 The Mecrisp Pocket authors. GPL-3.0-or-later.
 */
package io.github.leukoplast1980.mecrisppocket.vm

object NativeVm {
    init {
        System.loadLibrary("pocketjni")
    }

    @JvmStatic external fun create(virtualTime: Boolean): Long
    @JvmStatic external fun destroy(handle: Long)
    @JvmStatic external fun loadFlash(handle: Long, image: ByteArray)
    @JvmStatic external fun copyFlashIfDirty(handle: Long): ByteArray?

    /** Blocks the calling thread, running Forth until [shutdown]. */
    @JvmStatic external fun run(handle: Long)
    @JvmStatic external fun shutdown(handle: Long)
    @JvmStatic external fun pause(handle: Long, paused: Boolean)
    @JvmStatic external fun step(handle: Long)
    @JvmStatic external fun requestBreak(handle: Long)
    @JvmStatic external fun requestReset(handle: Long, safeBoot: Boolean)

    @JvmStatic external fun uartWrite(handle: Long, data: ByteArray, offset: Int, length: Int): Int
    @JvmStatic external fun uartRead(handle: Long, buffer: ByteArray): Int
    @JvmStatic external fun waitingForKey(handle: Long): Boolean
}
