/*
 * Mecrisp Pocket - the editor widget: CodeView (line numbers, current line)
 * with our own Forth highlighting, undo and completion on top.
 * Copyright (C) 2026 The Mecrisp Pocket authors. GPL-3.0-or-later.
 */
package io.github.leukoplast1980.mecrisppocket.ui

import android.content.Context
import android.graphics.Typeface
import android.text.InputType
import android.util.AttributeSet
import android.view.Gravity
import android.view.inputmethod.EditorInfo
import com.amrdeveloper.codeview.CodeView
import io.github.leukoplast1980.mecrisppocket.R

class ForthEditor(context: Context, attrs: AttributeSet?) : CodeView(context, attrs) {

    /** Called when the cursor moves, for the suggestion strip. */
    var onCursorMoved: (() -> Unit)? = null

    init {
        typeface = Typeface.MONOSPACE
        // No keyboard suggestions or autocorrect: they mangle Forth words.
        inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE or
            InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
        imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI
        gravity = Gravity.TOP or Gravity.START
        setHorizontallyScrolling(true)
        setEnableLineNumber(true)
        setLineNumberTypeface(Typeface.MONOSPACE)
        setLineNumberTextColor(context.getColor(R.color.label))
        setLineNumberTextSize(textSize * 0.85f)
        setEnableHighlightCurrentLine(true)
        setHighlightCurrentLineColor(context.getColor(R.color.current_line))
        setEnableAutoIndentation(false)
    }

    override fun onSelectionChanged(selStart: Int, selEnd: Int) {
        super.onSelectionChanged(selStart, selEnd)
        onCursorMoved?.invoke()
    }
}
