/*
 * Mecrisp Pocket - carries out what the board asks of the phone: vibration,
 * screen brightness and tones (see docs/board.md).
 * Copyright (C) 2026 The Mecrisp Pocket authors. GPL-3.0-or-later.
 */
package io.github.leukoplast1980.mecrisppocket.devices

import android.app.Activity
import android.media.AudioAttributes
import android.os.Build
import android.os.VibrationAttributes
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.view.WindowManager
import io.github.leukoplast1980.mecrisppocket.vm.VmHost

class PhoneDevices(private val activity: Activity, private val vm: VmHost) {

    private val events = IntArray(MAX_EVENTS * 5)
    private val tone = ToneOutput()

    private val vibrator: Vibrator? =
        if (Build.VERSION.SDK_INT >= 31) activity.getSystemService(VibratorManager::class.java)?.defaultVibrator
        else activity.getSystemService(Vibrator::class.java)

    /** Handles the events the board sent since the last call. UI thread. */
    fun poll() {
        while (true) {
            val n = vm.pollEvents(events)
            for (i in 0 until n) {
                val o = i * 5
                handle(events[o], events[o + 1], events[o + 2], events[o + 3], events[o + 4])
            }
            if (n < MAX_EVENTS) return
        }
    }

    private fun handle(type: Int, a: Int, b: Int, c: Int, d: Int) {
        when (type) {
            VIBRATE -> vibrate(a)
            BRIGHTNESS -> setBrightness(a)
            TONE -> tone.play(a, b, c, d)
        }
    }

    /**
     * Without a usage, Android files a short vibration under touch feedback
     * and drops it when the phone's touch vibration is off (a common
     * setting). The program asked for it explicitly, so it goes out as an
     * alarm vibration, which phones play even in silent mode.
     */
    private fun vibrate(ms: Int) {
        val v = vibrator ?: return
        if (ms <= 0) {
            v.cancel()
            return
        }
        val effect = VibrationEffect.createOneShot(ms.toLong(), VibrationEffect.DEFAULT_AMPLITUDE)
        if (Build.VERSION.SDK_INT >= 33) {
            v.vibrate(effect, VibrationAttributes.createForUsage(VibrationAttributes.USAGE_ALARM))
        } else {
            @Suppress("DEPRECATION")
            v.vibrate(effect, AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_ALARM).build())
        }
    }

    /** 0..255, or -1 for the system setting. 0 stays readable: a dark screen could not be reset. */
    private fun setBrightness(value: Int) {
        val attrs = activity.window.attributes
        attrs.screenBrightness = if (value < 0) WindowManager.LayoutParams.BRIGHTNESS_OVERRIDE_NONE
        else maxOf(value / 255f, MIN_BRIGHTNESS)
        activity.window.attributes = attrs
    }

    /** The app went to the background: no sound or vibration from it there. */
    fun quiet() {
        tone.stop()
        vibrator?.cancel()
    }

    fun release() {
        tone.release()
        vibrator?.cancel()
    }

    companion object {
        // Event types, as in emu/include/qp/pocket.h
        const val VIBRATE = 1
        const val BRIGHTNESS = 2
        const val TONE = 3
        private const val MAX_EVENTS = 64
        private const val MIN_BRIGHTNESS = 0.02f
    }
}
