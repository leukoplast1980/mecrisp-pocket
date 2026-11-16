/*
 * Mecrisp Pocket - the board's tone generator: one voice, four waveforms.
 * Pure Kotlin, so it can be unit tested; ToneOutput plays what it renders.
 * Copyright (C) 2026 The Mecrisp Pocket authors. GPL-3.0-or-later.
 */
package io.github.leukoplast1980.mecrisppocket.devices

import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.floor
import kotlin.math.min
import kotlin.math.sin

class ToneSynth(private val sampleRate: Int) {

    private var phase = 0.0          // 0 until 1
    private var step = 0.0           // phase advance per sample
    private var wave = 0
    private var level = 0.0          // current amplitude, 0..1
    private var target = 0.0         // amplitude while the tone lasts
    private var framesLeft = 0L

    /** Fades in and out over this time, so starts and stops do not click. */
    private val fadeStep = 1.0 / (sampleRate * FADE_S)

    /**
     * Starts or updates the tone: [ms] from now, or stops it when [ms] or
     * [hz] is 0. The phase carries on, so a frequency sweep stays smooth.
     */
    fun set(hz: Int, ms: Int, waveform: Int, volume: Int) {
        step = hz.coerceIn(0, MAX_HZ).toDouble() / sampleRate
        wave = waveform and 3
        framesLeft = if (hz > 0) ms.coerceAtLeast(0).toLong() * sampleRate / 1000 else 0
        target = volume.coerceIn(0, 100) / 100.0 * GAIN[wave]
    }

    /** Fades out now. */
    fun stop() {
        framesLeft = 0
    }

    /** True while there is something to play (the tone, or its fade-out). */
    val active: Boolean get() = framesLeft > 0 || level > 0.0

    /** Renders [count] 16-bit samples into [out]. */
    fun render(out: ShortArray, count: Int) {
        for (i in 0 until count) {
            val goal = if (framesLeft > 0) target else 0.0
            if (framesLeft > 0) framesLeft--
            level = when {
                level < goal -> min(goal, level + fadeStep)
                level > goal -> (level - fadeStep).coerceAtLeast(goal)
                else -> level
            }
            val s = if (level == 0.0) 0.0 else level * shape(wave, phase)
            out[i] = (s * PEAK).toInt().toShort()
            phase += step
            if (phase >= 1.0) phase -= floor(phase)
        }
    }

    companion object {
        const val MAX_HZ = 20000
        private const val FADE_S = 0.003
        private const val PEAK = 32767 * 0.8

        /** Rough loudness match between the waveforms. */
        private val GAIN = doubleArrayOf(1.0, 0.5, 1.0, 0.6)

        /** One period of each waveform, at phase 0 until 1, in -1..1. */
        fun shape(wave: Int, phase: Double): Double = when (wave) {
            0 -> sin(2 * PI * phase)
            1 -> if (phase < 0.5) 1.0 else -1.0
            2 -> 4 * abs(phase - 0.5) - 1
            else -> 2 * phase - 1
        }
    }
}
