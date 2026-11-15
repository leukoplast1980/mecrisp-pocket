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

    /** Sends one line and waits for Forth: 0 ok, 1 error, 2 timeout. Blocks. */
    @JvmStatic external fun sendLine(handle: Long, line: ByteArray, timeoutMs: Int): Int

    /** See [Snapshot] for the layout. */
    @JvmStatic external fun snapshot(handle: Long): IntArray
    @JvmStatic external fun deviceRegs(handle: Long, addresses: IntArray): IntArray
    @JvmStatic external fun readMemory(handle: Long, address: Int, length: Int): ByteArray?

    /**
     * Returns { names: ByteArray (UTF-8, concatenated), nameLengths: IntArray,
     * headers: IntArray, codes: IntArray, flags: IntArray } in search order.
     * layout = CoreDictionaryAnfang, Dictionarypointer, Fadenende, ZweitFadenende.
     */
    @JvmStatic external fun dictionary(handle: Long, layout: IntArray): Array<Any>?
}
