package io.github.leukoplast1980.mecrisppocket.gpio

import org.junit.Assert.assertEquals
import org.junit.Test

class GpioTraceTest {

    /** JNI layout: out[0] = first index (unused by merge), then time, bits pairs. */
    private fun data(vararg entries: Pair<Long, Long>): LongArray {
        val d = LongArray(1 + entries.size * 2)
        for ((i, e) in entries.withIndex()) {
            d[1 + 2 * i] = e.first
            d[2 + 2 * i] = e.second
        }
        return d
    }

    private fun bits(levels: Int, toggled: Int, busy: Int = 0) =
        levels.toLong() or (toggled.toLong() shl 16) or (busy.toLong() shl 32)

    @Test fun mergeAndReadBack() {
        val t = GpioTrace(8)
        t.merge(0, data(100L to bits(1, 1), 300L to bits(3, 2, 2)), 2)
        assertEquals(2, t.size)
        assertEquals(1L, t.nextRead)               // the newest entry is read again
        assertEquals(3, t.levels(1))
        assertEquals(2, t.toggled(1))
        assertEquals(2, t.busy(1))
        assertEquals(0, t.levelsBefore(0))
        assertEquals(1, t.levelsBefore(1))
    }

    @Test fun newestEntryIsReplaced() {
        val t = GpioTrace(8)
        t.merge(0, data(100L to bits(1, 1), 300L to bits(3, 2)), 2)
        t.merge(1, data(300L to bits(1, 2, 2), 500L to bits(0, 1)), 2)
        assertEquals(3, t.size)
        assertEquals(1, t.levels(1))
        assertEquals(2, t.busy(1))
        assertEquals(0, t.levels(2))
    }

    @Test fun ringDropsTheOldest() {
        val t = GpioTrace(4)
        t.merge(0, data(*Array(6) { (it * 100L) to bits(it and 1, 1) }), 6)
        assertEquals(4, t.size)
        assertEquals(200L, t.time(0))
        assertEquals(5L, t.nextRead)
    }

    @Test fun gapStartsOver() {
        val t = GpioTrace(8)
        t.merge(0, data(100L to bits(1, 1)), 1)
        t.merge(10, data(900L to bits(0, 1)), 1)
        assertEquals(1, t.size)
        assertEquals(900L, t.time(0))
        assertEquals(10L, t.nextRead)
    }

    @Test fun findByTime() {
        val t = GpioTrace(8)
        t.merge(0, data(100L to bits(1, 1), 200L to bits(0, 1), 300L to bits(1, 1)), 3)
        assertEquals(0, t.firstAtOrAfter(50))
        assertEquals(1, t.firstAtOrAfter(200))
        assertEquals(2, t.firstAtOrAfter(201))
        assertEquals(3, t.firstAtOrAfter(301))
    }
}
