/*
 * Mecrisp Pocket - the editor widget: CodeView (current line highlight)
 * with our own line number gutter, Forth highlighting, undo and completion
 * on top.
 * Copyright (C) 2026 The Mecrisp Pocket authors. GPL-3.0-or-later.
 */
package io.github.leukoplast1980.mecrisppocket.ui

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Typeface
import android.text.InputType
import android.text.Selection
import android.util.AttributeSet
import android.view.Gravity
import android.view.inputmethod.EditorInfo
import com.amrdeveloper.codeview.CodeView
import io.github.leukoplast1980.mecrisppocket.R
import kotlin.math.max
import kotlin.math.roundToInt

/**
 * CodeView's own line numbers are drawn into a left padding that is too
 * narrow for the numbers (they overlap the text), move away when the text
 * scrolls sideways, and reset the padding on every frame. So we draw the
 * gutter ourselves: it stays put when scrolling sideways, and the padding is
 * only changed when the number of digits changes.
 *
 * Lines never wrap (horizontal scrolling), so layout line i is source line
 * i + 1.
 */
class ForthEditor(context: Context, attrs: AttributeSet?) : CodeView(context, attrs) {

    /** Called when the cursor moves, for the suggestion strip. */
    var onCursorMoved: (() -> Unit)? = null

    private val numberPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        typeface = Typeface.MONOSPACE
        textAlign = Paint.Align.RIGHT
    }
    private val gutterPaint = Paint().apply { color = context.getColor(R.color.console_background) }
    private val dividerPaint = Paint().apply {
        color = context.getColor(R.color.divider)
        strokeWidth = resources.displayMetrics.density
    }
    private val numberColor = context.getColor(R.color.label)
    private val currentNumberColor = context.getColor(R.color.console_text)

    private var digits = 0
    private var numbersRight = 0f
    private var gutterRight = 0f
    private var gutterUpdatePosted = false

    init {
        typeface = Typeface.MONOSPACE
        // No keyboard suggestions or autocorrect: they mangle Forth words.
        inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE or
            InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
        imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI
        gravity = Gravity.TOP or Gravity.START
        setHorizontallyScrolling(true)
        setEnableLineNumber(false)
        setEnableHighlightCurrentLine(true)
        setHighlightCurrentLineColor(context.getColor(R.color.current_line))
        setEnableAutoIndentation(false)
        numberPaint.textSize = textSize * 0.85f
        updateGutter(2)
    }

    /** At least two digits, so the text does not jump between line 9 and 10. */
    private fun digitsFor(lines: Int) = max(2, lines.toString().length)

    private fun updateGutter(newDigits: Int) {
        digits = newDigits
        val charWidth = numberPaint.measureText("8")
        numbersRight = charWidth * (0.6f + digits)
        gutterRight = numbersRight + charWidth * 0.6f
        val left = (gutterRight + charWidth * 0.8f).roundToInt()
        if (left != paddingLeft) setPadding(left, paddingTop, paddingRight, paddingBottom)
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val lay = layout ?: return

        // Changing the padding while drawing would start another layout pass
        // from inside this one; do it right after.
        if (digitsFor(lay.lineCount) != digits && !gutterUpdatePosted) {
            gutterUpdatePosted = true
            post {
                gutterUpdatePosted = false
                val d = digitsFor(lineCount)
                if (d != digits) updateGutter(d)
            }
        }

        // The gutter is drawn in view coordinates so it stays put when the
        // text scrolls sideways; it covers the current line highlight there.
        val left = scrollX.toFloat()
        val top = scrollY.toFloat()
        canvas.drawRect(left, top, left + gutterRight, top + height, gutterPaint)
        canvas.drawLine(left + gutterRight, top, left + gutterRight, top + height, dividerPaint)

        val contentTop = scrollY - extendedPaddingTop
        val first = lay.getLineForVertical(max(0, contentTop))
        val last = lay.getLineForVertical(contentTop + height)
        val cursor = Selection.getSelectionStart(text)
        val cursorLine = if (cursor >= 0) lay.getLineForOffset(cursor) else -1
        val x = left + numbersRight
        for (i in first..last) {
            numberPaint.color = if (i == cursorLine) currentNumberColor else numberColor
            canvas.drawText((i + 1).toString(), x, getLineBounds(i, null).toFloat(), numberPaint)
        }
    }

    override fun onSelectionChanged(selStart: Int, selEnd: Int) {
        super.onSelectionChanged(selStart, selEnd)
        onCursorMoved?.invoke()
    }
}
