/*
 * Mecrisp Pocket - owns the one Forth VM of the app and its flash image.
 * Copyright (C) 2026 The Mecrisp Pocket authors. GPL-3.0-or-later.
 */
package io.github.leukoplast1980.mecrisppocket.vm

import android.content.Context
import android.util.Log
import io.github.leukoplast1980.mecrisppocket.forth.Dictionary
import org.json.JSONObject
import java.io.File
import java.io.IOException

/**
 * App-wide host of the Forth VM. It survives activity recreation; the
 * activity pauses it when the app leaves the foreground.
 *
 * Flash persistence: the saved image (flash.img) is kept together with the
 * hash of the shipped image it started from (flash.core). If an app update
 * ships a different core, the saved image keeps running (its words call
 * into the old core) and [coreUpdateAvailable] is set.
 */
class VmHost private constructor(context: Context) {

    private val files = context.filesDir
    private val flashFile = File(files, "flash.img")
    private val coreFile = File(files, "flash.core")

    val handle: Long = NativeVm.create(false)

    /** Hash of the core image shipped with this app version. */
    val shippedCoreHash: String

    /** True when the saved flash image was built on a different core. */
    var coreUpdateAvailable = false
        private set

    private val shippedImage: ByteArray

    /** Core symbol addresses from pocket.json (CoreDictionaryAnfang, ...). */
    val symbols: Map<String, Int>

    private val dictionaryLayout: IntArray

    /** The words Forth could find at the last [refreshDictionary]. */
    @Volatile var dictionary: Dictionary = Dictionary.EMPTY
        private set

    private var foreground = false
    private var userPaused = false

    init {
        val assets = context.assets
        shippedImage = assets.open("pocket.img").use { it.readBytes() }
        val meta = assets.open("pocket.json").use { JSONObject(it.reader().readText()) }
        shippedCoreHash = meta.getString("imageSha256")
        val syms = meta.getJSONObject("symbols")
        symbols = syms.keys().asSequence().associateWith {
            java.lang.Long.decode(syms.getString(it)).toInt()
        }
        dictionaryLayout = intArrayOf(
            symbols.getValue("CoreDictionaryAnfang"),
            symbols.getValue("Dictionarypointer"),
            symbols.getValue("Fadenende"),
            symbols.getValue("ZweitFadenende"),
        )

        val saved = readSaved()
        if (saved != null) {
            NativeVm.loadFlash(handle, saved)
            val savedCore = runCatching { coreFile.readText().trim() }.getOrNull()
            coreUpdateAvailable = savedCore != null && savedCore != shippedCoreHash
        } else {
            NativeVm.loadFlash(handle, shippedImage)
            writeCoreHash(shippedCoreHash)
        }

        Thread({ NativeVm.run(handle) }, "forth-vm").apply {
            isDaemon = true
            start()
        }
    }

    private fun readSaved(): ByteArray? =
        try {
            if (flashFile.length() == FLASH_SIZE.toLong()) flashFile.readBytes() else null
        } catch (e: IOException) {
            Log.w(TAG, "cannot read saved flash", e)
            null
        }

    private fun writeCoreHash(hash: String) {
        try {
            coreFile.writeText(hash)
        } catch (e: IOException) {
            Log.w(TAG, "cannot write core hash", e)
        }
    }

    /** Writes the flash image to storage if it changed. Atomic: temp file + rename. */
    @Synchronized
    fun saveFlash() {
        val image = NativeVm.copyFlashIfDirty(handle) ?: return
        val tmp = File(files, "flash.img.tmp")
        try {
            tmp.outputStream().use { out ->
                out.write(image)
                out.fd.sync()
            }
            if (!tmp.renameTo(flashFile)) throw IOException("rename failed")
        } catch (e: IOException) {
            Log.e(TAG, "cannot save flash", e)
        }
    }

    /** Discards the saved image and starts over from the shipped core. */
    @Synchronized
    fun switchToShippedCore() {
        NativeVm.pause(handle, true)
        NativeVm.loadFlash(handle, shippedImage)
        flashFile.delete()
        writeCoreHash(shippedCoreHash)
        coreUpdateAvailable = false
        NativeVm.requestReset(handle, false)
        NativeVm.pause(handle, false)
    }

    /** The activity is visible: the VM may run unless the user paused it. */
    @Synchronized
    fun setForeground(visible: Boolean) {
        foreground = visible
        NativeVm.pause(handle, !foreground || userPaused)
    }

    /** Pause button of the Registers view. */
    @Synchronized
    fun setUserPaused(paused: Boolean) {
        userPaused = paused
        NativeVm.pause(handle, !foreground || userPaused)
    }

    val isUserPaused: Boolean @Synchronized get() = userPaused

    fun step() = NativeVm.step(handle)

    fun snapshot(): Snapshot = Snapshot(NativeVm.snapshot(handle))
    fun deviceRegs(addresses: IntArray): IntArray = NativeVm.deviceRegs(handle, addresses)
    fun readMemory(address: Int, length: Int): ByteArray? = NativeVm.readMemory(handle, address, length)
    fun waitingForKey(): Boolean = NativeVm.waitingForKey(handle)

    /** Re-reads the dictionary from guest memory. Call off the UI thread. */
    fun refreshDictionary(): Dictionary {
        val d = Dictionary.fromNative(
            NativeVm.dictionary(handle, dictionaryLayout),
            symbols.getValue("FlashDictionaryAnfang"),
        )
        dictionary = d
        return d
    }
    fun stop() = NativeVm.requestBreak(handle)
    fun reset(safeBoot: Boolean) = NativeVm.requestReset(handle, safeBoot)

    /** Queues text for Forth's input. Returns false if the input buffer is full. */
    fun send(text: String): Boolean {
        val bytes = text.toByteArray(Charsets.UTF_8)
        var off = 0
        var tries = 0
        while (off < bytes.size && tries < 100) {
            val n = NativeVm.uartWrite(handle, bytes, off, bytes.size - off)
            if (n == 0) { Thread.sleep(2); tries++ } else off += n
        }
        return off == bytes.size
    }

    fun read(buffer: ByteArray): Int = NativeVm.uartRead(handle, buffer)

    companion object {
        private const val TAG = "VmHost"
        const val FLASH_SIZE = 256 * 1024

        @Volatile private var instance: VmHost? = null

        fun get(context: Context): VmHost =
            instance ?: synchronized(this) {
                instance ?: VmHost(context.applicationContext).also { instance = it }
            }
    }
}
