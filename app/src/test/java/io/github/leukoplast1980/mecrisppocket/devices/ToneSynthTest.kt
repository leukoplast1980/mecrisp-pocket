package io.github.leukoplast1980.mecrisppocket.devices

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.abs

class ToneSynthTest {

    private val rate = 48000

    private fun render(s: ToneSynth, n: Int) = ShortArray(n).also { s.render(it, n) }

    @Test fun silentUntilStarted() {
        val s = ToneSynth(rate)
        assertFalse(s.active)
        assertTrue(render(s, 100).all { it.toInt() == 0 })
    }

    @Test fun playsForItsDurationThenFadesOut() {
        val s = ToneSynth(rate)
        s.set(1000, 10, 0, 100)                     // 10 ms = 480 samples
        val out = render(s, 480 + 480)
        assertTrue(out.take(480).maxOf { abs(it.toInt()) } > 20000)
        assertTrue(out.takeLast(100).all { it.toInt() == 0 })  // faded out after 3 ms
        assertFalse(s.active)
    }

    @Test fun fadesInWithoutAJump() {
        val s = ToneSynth(rate)
        s.set(1000, 100, 1, 100)                    // square: full value at once without a fade
        val out = render(s, 10)
        assertTrue(abs(out[0].toInt()) < 1000)
    }

    @Test fun volumeScales() {
        val loud = ToneSynth(rate).also { it.set(500, 100, 0, 100) }
        val quiet = ToneSynth(rate).also { it.set(500, 100, 0, 25) }
        val a = render(loud, 4800).maxOf { abs(it.toInt()) }
        val b = render(quiet, 4800).maxOf { abs(it.toInt()) }
        assertEquals(0.25, b.toDouble() / a, 0.02)
    }

    @Test fun frequencyIsRight() {
        val s = ToneSynth(rate)
        s.set(1000, 1000, 1, 100)                   // square wave: count sign changes
        val out = render(s, rate)                   // one second
        var changes = 0
        for (i in 1 until out.size) if ((out[i] >= 0) != (out[i - 1] >= 0)) changes++
        assertEquals(2000.0, changes.toDouble(), 4.0)
    }

    @Test fun stopFadesOut() {
        val s = ToneSynth(rate)
        s.set(440, 10_000, 0, 100)
        render(s, 1000)
        s.stop()
        render(s, 1000)
        assertFalse(s.active)
    }

    @Test fun shapes() {
        assertEquals(0.0, ToneSynth.shape(0, 0.0), 1e-9)
        assertEquals(1.0, ToneSynth.shape(0, 0.25), 1e-9)
        assertEquals(1.0, ToneSynth.shape(1, 0.1), 1e-9)
        assertEquals(-1.0, ToneSynth.shape(1, 0.6), 1e-9)
        assertEquals(-1.0, ToneSynth.shape(2, 0.5), 1e-9)
        assertEquals(1.0, ToneSynth.shape(2, 0.0), 1e-9)
        assertEquals(-1.0, ToneSynth.shape(3, 0.0), 1e-9)
    }
}
