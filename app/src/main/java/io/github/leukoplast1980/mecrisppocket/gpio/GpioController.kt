/*
 * Mecrisp Pocket - the GPIO tab: reads the board's pins and trace while it
 * is visible, and drives input pins when they are tapped.
 * Copyright (C) 2026 The Mecrisp Pocket authors. GPL-3.0-or-later.
 */
package io.github.leukoplast1980.mecrisppocket.gpio

import android.app.Activity
import android.widget.Toast
import com.google.android.material.button.MaterialButtonToggleGroup
import io.github.leukoplast1980.mecrisppocket.R
import io.github.leukoplast1980.mecrisppocket.vm.VmHost

class GpioController(private val activity: Activity, private val vm: VmHost) {

    val view: GpioView = activity.findViewById(R.id.gpioView)
    private val trace = GpioTrace()
    private val state = LongArray(6)

    /** The first read can bring the whole trace the board keeps; later ones only what is new. */
    private val firstBuffer by lazy { LongArray(1 + 2 * 65536) }
    private val buffer = LongArray(1 + 2 * 8192)
    private var primed = false

    init {
        view.trace = trace
        view.onPinTapped = ::tapped
        val group = activity.findViewById<MaterialButtonToggleGroup>(R.id.gpioWindow)
        group.check(R.id.gpioWindow5s)
        group.addOnButtonCheckedListener { _, id, checked ->
            if (!checked) return@addOnButtonCheckedListener
            view.windowUs = when (id) {
                R.id.gpioWindow50ms -> 50_000L
                R.id.gpioWindow500ms -> 500_000L
                else -> 5_000_000L
            }
        }
    }

    /** Reads the pins and the new trace entries and redraws. UI thread, while visible. */
    fun refresh() {
        vm.gpioState(state)
        val buf = if (primed) buffer else firstBuffer
        val n = vm.gpioTrace(if (primed) trace.nextRead else 0, buf)
        trace.merge(buf[0], buf, n)
        primed = true
        view.update(state[0].toInt(), state[3].toInt(), state[4])
    }

    private fun tapped(pin: Int) {
        val mask = 1 shl pin
        if ((state[0].toInt() and mask) != 0) {
            Toast.makeText(activity, activity.getString(R.string.gpio_output_pin, pin), Toast.LENGTH_SHORT).show()
            return
        }
        val high = (state[2].toInt() and mask) != 0
        vm.gpioSetInput(mask, if (high) 0 else mask)
        refresh()
    }
}
