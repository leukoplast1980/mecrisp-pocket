/*
 * Mecrisp Pocket - plays ToneSynth through an AudioTrack on its own thread.
 * The track runs only while a tone (or its fade-out) is playing.
 * Copyright (C) 2026 The Mecrisp Pocket authors. GPL-3.0-or-later.
 */
package io.github.leukoplast1980.mecrisppocket.devices

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioTrack
import android.util.Log

class ToneOutput {

    private val lock = Object()
    private var pending: IntArray? = null    // hz, ms, waveform, volume
    private var stopRequested = false
    private var running = false
    private var thread: Thread? = null

    /** Starts, changes or (with ms = 0) stops the tone. */
    fun play(hz: Int, ms: Int, waveform: Int, volume: Int) {
        synchronized(lock) {
            pending = intArrayOf(hz, ms, waveform, volume)
            if (!running) start()
            lock.notifyAll()
        }
    }

    /** Fades the tone out. Nothing to do (and no thread to start) if none is playing. */
    fun stop() {
        synchronized(lock) { if (!running) return }
        play(0, 0, 0, 0)
    }

    /** Stops the thread and frees the audio track. */
    fun release() {
        val t: Thread?
        synchronized(lock) {
            stopRequested = true
            lock.notifyAll()
            t = thread
        }
        t?.join(500)
        synchronized(lock) {
            thread = null
            running = false
            stopRequested = false
        }
    }

    private fun start() {
        running = true
        stopRequested = false
        thread = Thread({ loop() }, "Mecrisp Pocket tone").apply { start() }
    }

    private fun loop() {
        val rate = AudioTrack.getNativeOutputSampleRate(AudioManager.STREAM_MUSIC).takeIf { it > 0 } ?: 48000
        val chunk = rate / 200   // 5 ms
        val minBytes = AudioTrack.getMinBufferSize(rate, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT)
        val track = try {
            AudioTrack.Builder()
                .setAudioAttributes(
                    AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_GAME)
                        .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                        .build()
                )
                .setAudioFormat(
                    AudioFormat.Builder()
                        .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                        .setSampleRate(rate)
                        .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                        .build()
                )
                .setTransferMode(AudioTrack.MODE_STREAM)
                .setPerformanceMode(AudioTrack.PERFORMANCE_MODE_LOW_LATENCY)
                .setBufferSizeInBytes(maxOf(minBytes, chunk * 2 * 4))
                .build()
        } catch (e: Exception) {
            Log.w(TAG, "no audio track", e)
            synchronized(lock) { running = false; thread = null }
            return
        }

        val synth = ToneSynth(rate)
        val buf = ShortArray(chunk)
        var playing = false
        try {
            while (true) {
                synchronized(lock) {
                    while (!stopRequested && pending == null && !synth.active) {
                        if (playing) {
                            track.pause()
                            track.flush()
                            playing = false
                        }
                        lock.wait()
                    }
                    if (stopRequested) return
                    pending?.let { p ->
                        if (p[1] == 0 || p[0] == 0) synth.stop() else synth.set(p[0], p[1], p[2], p[3])
                    }
                    pending = null
                }
                if (!playing) {
                    track.play()
                    playing = true
                }
                synth.render(buf, chunk)
                track.write(buf, 0, chunk)   // blocks: paces the loop
            }
        } catch (e: Exception) {
            Log.w(TAG, "tone thread", e)
        } finally {
            runCatching { track.stop() }
            track.release()
            synchronized(lock) { running = false; thread = null }
        }
    }

    private companion object {
        const val TAG = "ToneOutput"
    }
}
