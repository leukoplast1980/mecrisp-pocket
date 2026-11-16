/*
 * Mecrisp Pocket - the app's copy of the board's GPIO trace (level changes
 * with their times), kept in a ring, for drawing the logic trace.
 * Pure Kotlin, so it can be unit tested.
 * Copyright (C) 2026 The Mecrisp Pocket authors. GPL-3.0-or-later.
 */
package io.github.leukoplast1980.mecrisppocket.gpio

class GpioTrace(private val capacity: Int = 65536) {

    private val times = LongArray(capacity)
    private val bits = LongArray(capacity)  // levels | toggled << 16 | busy << 32
    private var head = 0                    // ring position of the oldest entry
    var size = 0
        private set

    /** Board index of the oldest entry kept (entries are numbered by the board). */
    private var firstIndex = 0L

    /** Where the next read should start: the newest entry again, since it can still grow. */
    val nextRead: Long get() = if (size == 0) firstIndex else firstIndex + size - 1

    /**
     * Merges what the board returned: [count] entries numbered from [first],
     * in [data] as 2 longs each from data[1] on (the JNI layout).
     */
    fun merge(first: Long, data: LongArray, count: Int) {
        if (count == 0) return
        if (size == 0 || first < firstIndex || first > firstIndex + size) {
            // Nothing kept yet, or a gap: start over from what came.
            head = 0
            size = 0
            firstIndex = first
        } else {
            size = (first - firstIndex).toInt()   // drop entries the board sent again
        }
        for (i in 0 until count) {
            if (size == capacity) {
                head = (head + 1) % capacity
                size--
                firstIndex++
            }
            val p = (head + size) % capacity
            times[p] = data[1 + 2 * i]
            bits[p] = data[2 + 2 * i]
            size++
        }
    }

    fun clear() {
        head = 0
        size = 0
        firstIndex = 0
    }

    fun time(i: Int): Long = times[(head + i) % capacity]
    fun levels(i: Int): Int = (bits[(head + i) % capacity] and 0xffff).toInt()
    fun toggled(i: Int): Int = ((bits[(head + i) % capacity] shr 16) and 0xffff).toInt()
    fun busy(i: Int): Int = ((bits[(head + i) % capacity] shr 32) and 0xffff).toInt()

    /** Index (0 until size) of the first entry at or after [t], or size. */
    fun firstAtOrAfter(t: Long): Int {
        var lo = 0
        var hi = size
        while (lo < hi) {
            val mid = (lo + hi) ushr 1
            if (time(mid) < t) lo = mid + 1 else hi = mid
        }
        return lo
    }

    /**
     * Pin levels just before entry [i]: the previous entry's levels, or for
     * the oldest one its own levels with the changed pins flipped back.
     */
    fun levelsBefore(i: Int): Int =
        if (i > 0) levels(i - 1) else if (size > 0) levels(0) xor toggled(0) else 0
}
