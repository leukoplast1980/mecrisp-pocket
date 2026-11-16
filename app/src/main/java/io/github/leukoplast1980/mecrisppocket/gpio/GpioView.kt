/*
 * Mecrisp Pocket - the GPIO view: one row per pin with its number,
 * direction and an LED for its level, and a logic trace of the last few
 * seconds. A tap on an input pin's row start toggles the level the outside
 * world drives on it.
 * Copyright (C) 2026 The Mecrisp Pocket authors. GPL-3.0-or-later.
 */
package io.github.leukoplast1980.mecrisppocket.gpio

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Typeface
import android.util.AttributeSet
import android.util.TypedValue
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import io.github.leukoplast1980.mecrisppocket.R
import kotlin.math.abs
import kotlin.math.max

class GpioView(context: Context, attrs: AttributeSet?) : View(context, attrs) {

    /** Called with the pin number when the start of a row is tapped. */
    var onPinTapped: ((Int) -> Unit)? = null

    var trace: GpioTrace? = null

    /** Shown time span in µs, ending at the board's current time. */
    var windowUs = 5_000_000L
        set(v) { field = v; invalidate() }

    private var dir = 0
    private var levels = 0
    private var nowUs = 0L

    private val dp = resources.displayMetrics.density
    private val rowHeight = 32 * dp
    private val headerHeight = 22 * dp
    private val pad = 8 * dp
    private val ledRadius = 6 * dp

    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        typeface = Typeface.MONOSPACE
        textSize = sp(13f)
        color = context.getColor(R.color.console_text)
    }
    private val dimTextPaint = Paint(textPaint).apply {
        color = context.getColor(R.color.label)
        textSize = sp(11f)
    }
    private val highColor = context.getColor(R.color.accent)
    private val lowColor = context.getColor(R.color.label)
    private val ledOffColor = context.getColor(R.color.chip_background)
    private val highPaint = Paint().apply { color = highColor; strokeWidth = 2 * dp }
    private val lowPaint = Paint().apply { color = lowColor; strokeWidth = 2 * dp }
    private val busyPaint = Paint().apply { color = highColor; alpha = 110 }
    private val edgePaint = Paint().apply { color = highColor }
    private val ledPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val ringPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 1.5f * dp
    }
    private val stripePaint = Paint().apply { color = context.getColor(R.color.chip_background); alpha = 90 }
    private val gridPaint = Paint().apply { color = context.getColor(R.color.divider); strokeWidth = dp }

    /** Where the trace starts: after "P15", "out" and the LED. */
    private val traceLeft = pad + textPaint.measureText("P15 ") + dimTextPaint.measureText("out ") +
        2 * ledRadius + 2 * pad

    /** Per pin and column: HIGH, LOW (both: an edge) and BUSY bits. */
    private var cols = ByteArray(0)

    fun update(dir: Int, levels: Int, nowUs: Long) {
        this.dir = dir
        this.levels = levels
        this.nowUs = nowUs
        invalidate()
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val w = MeasureSpec.getSize(widthMeasureSpec)
        val h = (headerHeight + PINS * rowHeight + pad).toInt()
        setMeasuredDimension(w, resolveSize(h, heightMeasureSpec))
    }

    override fun onDraw(canvas: Canvas) {
        val right = width - pad
        val traceWidth = (right - traceLeft).toInt().coerceAtLeast(1)

        // Time axis
        val base = headerHeight - 7 * dp
        canvas.drawText("-" + formatSpan(windowUs), traceLeft, base, dimTextPaint)
        val now = "now"
        canvas.drawText(now, right - dimTextPaint.measureText(now), base, dimTextPaint)
        canvas.drawLine(traceLeft, headerHeight - 2 * dp, right, headerHeight - 2 * dp, gridPaint)

        rasterize(traceWidth)

        for (pin in 0 until PINS) {
            val top = headerHeight + pin * rowHeight
            if (pin % 2 == 1) canvas.drawRect(0f, top, width.toFloat(), top + rowHeight, stripePaint)
            val mid = top + rowHeight / 2
            val textY = mid + textPaint.textSize / 3
            val output = bit(dir, pin)
            val high = bit(levels, pin)

            canvas.drawText("P$pin", pad, textY, textPaint)
            val dirX = pad + textPaint.measureText("P15 ")
            canvas.drawText(if (output) "out" else "in", dirX, textY, dimTextPaint)

            // LED: filled for outputs; a ring around it for inputs (tappable).
            val ledX = traceLeft - pad - ledRadius
            ledPaint.color = if (high) highColor else ledOffColor
            canvas.drawCircle(ledX, mid, ledRadius, ledPaint)
            if (!output) {
                ringPaint.color = if (high) highColor else lowColor
                canvas.drawCircle(ledX, mid, ledRadius + 3 * dp, ringPaint)
            }

            drawTrace(canvas, pin, traceWidth, top)
        }
    }

    /** Fills [cols] from the trace: what level each pin had in each pixel column. */
    private fun rasterize(width: Int) {
        if (cols.size < PINS * width) cols = ByteArray(PINS * width)
        java.util.Arrays.fill(cols, 0, PINS * width, 0)
        val t = trace
        val start = nowUs - windowUs
        val from = IntArray(PINS)          // column where the current level began
        var cur: Int
        var i0 = 0
        if (t == null || t.size == 0) {
            cur = levels
        } else {
            i0 = t.firstAtOrAfter(start)
            cur = if (i0 < t.size) t.levelsBefore(i0) else t.levels(t.size - 1)
        }
        val tickCols = max(1, (100L * width / windowUs.coerceAtLeast(1)).toInt())
        if (t != null) {
            for (i in i0 until t.size) {
                val c = colOf(t.time(i), start, width)
                val changed = t.toggled(i)
                val busy = t.busy(i)
                var m = changed
                while (m != 0) {
                    val pin = Integer.numberOfTrailingZeros(m)
                    m = m and (m - 1)
                    val old = if (bit(t.before(i), pin)) HIGH else LOW
                    mark(pin, width, from[pin], c, old)
                    val now = if (bit(t.levels(i), pin)) HIGH else LOW
                    mark(pin, width, c, c, now)
                    if (bit(busy, pin)) mark(pin, width, c, (c + tickCols - 1).coerceAtMost(width - 1), BUSY)
                    from[pin] = c
                }
                cur = t.levels(i)
            }
        }
        for (pin in 0 until PINS) {
            mark(pin, width, from[pin], width - 1, if (bit(cur, pin)) HIGH else LOW)
        }
    }

    private fun colOf(t: Long, start: Long, width: Int): Int =
        ((t - start).coerceAtLeast(0) * width / windowUs.coerceAtLeast(1)).toInt().coerceIn(0, width - 1)

    private fun mark(pin: Int, width: Int, from: Int, to: Int, bits: Int) {
        val base = pin * width
        for (c in from..to) cols[base + c] = (cols[base + c].toInt() or bits).toByte()
    }

    /** Draws runs of equal columns: a line at the high or low level, or a block where it changed. */
    private fun drawTrace(canvas: Canvas, pin: Int, width: Int, top: Float) {
        val hi = top + rowHeight * 0.28f
        val lo = top + rowHeight * 0.72f
        val base = pin * width
        var c = 0
        while (c < width) {
            val s = cols[base + c].toInt()
            var e = c + 1
            while (e < width && cols[base + e].toInt() == s) e++
            val x0 = traceLeft + c
            val x1 = traceLeft + e
            when {
                (s and BUSY) != 0 -> canvas.drawRect(x0, hi - dp, max(x1, x0 + 2 * dp), lo + dp, busyPaint)
                s == HIGH -> canvas.drawLine(x0, hi, x1, hi, highPaint)
                s == LOW -> canvas.drawLine(x0, lo, x1, lo, lowPaint)
                s == HIGH or LOW -> canvas.drawRect(x0, hi - dp, max(x1, x0 + 2 * dp), lo + dp, edgePaint)
            }
            c = e
        }
    }

    // Taps on the start of a row (number, direction, LED)

    private var downX = 0f
    private var downY = 0f
    private val slop = ViewConfiguration.get(context).scaledTouchSlop

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                downX = event.x
                downY = event.y
                return pinAt(event.x, event.y) >= 0
            }
            MotionEvent.ACTION_UP -> {
                if (abs(event.x - downX) < slop && abs(event.y - downY) < slop) {
                    val pin = pinAt(event.x, event.y)
                    if (pin >= 0) {
                        performClick()
                        onPinTapped?.invoke(pin)
                    }
                }
                return true
            }
        }
        return true
    }

    override fun performClick(): Boolean = super.performClick()

    /** The pin whose row start is at (x, y), or -1. */
    private fun pinAt(x: Float, y: Float): Int {
        if (x > traceLeft || y < headerHeight) return -1
        val pin = ((y - headerHeight) / rowHeight).toInt()
        return if (pin in 0 until PINS) pin else -1
    }

    private fun sp(v: Float) = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, v, resources.displayMetrics)

    companion object {
        const val PINS = 16
        private const val HIGH = 1
        private const val LOW = 2
        private const val BUSY = 4

        private fun bit(v: Int, pin: Int) = ((v shr pin) and 1) != 0

        fun formatSpan(us: Long): String = when {
            us >= 1_000_000 && us % 1_000_000 == 0L -> "${us / 1_000_000} s"
            us >= 1_000_000 -> "%.1f s".format(us / 1e6)
            us >= 1000 -> "${us / 1000} ms"
            else -> "$us µs"
        }
    }
}
