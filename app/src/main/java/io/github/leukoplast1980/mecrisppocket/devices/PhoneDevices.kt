/*
 * Mecrisp Pocket - carries out what the board asks of the phone: vibration,
 * screen brightness and tones (see docs/board.md). The board keeps these as
 * state; this polls it and acts on what changed.
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

    private val state = IntArray(8)
    private val tone = ToneOutput()

    private val vibrator: Vibrator? =
        if (Build.VERSION.SDK_INT >= 31) activity.getSystemService(VibratorManager::class.java)?.defaultVibrator
        else activity.getSystemService(Vibrator::class.java)

    // What was carried out last. Until the first poll nothing is known: then
    // the brightness is applied (a new window needs it again), but an old
    // vibration or tone is not replayed.
    private var primed = false
    private var vibrateSeq = 0
    private var toneSeq = 0
    private var brightness = Int.MIN_VALUE

    /**
     * Carries out what changed since the last call. UI thread. The board
     * keeps only the latest state, so a program that changes things faster
     * than this runs cannot leave the phone behind.
     */
    fun poll() {
        vm.phoneState(state)
        if (state[2] != brightness) {
            brightness = state[2]
            setBrightness(brightness)
        }
        if (primed && state[0] != vibrateSeq) vibrate(state[1])
        if (primed && state[3] != toneSeq) tone.play(state[4], state[5], state[6], state[7])
        vibrateSeq = state[0]
        toneSeq = state[3]
        primed = true
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

    private companion object {
        const val MIN_BRIGHTNESS = 0.02f
    }
}
