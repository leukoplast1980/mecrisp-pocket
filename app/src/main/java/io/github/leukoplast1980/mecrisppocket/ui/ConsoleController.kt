/*
 * Mecrisp Pocket - the REPL: output, line and raw input, and history.
 * Copyright (C) 2026 The Mecrisp Pocket authors. GPL-3.0-or-later.
 */
package io.github.leukoplast1980.mecrisppocket.ui

import android.app.Activity
import android.content.ClipData
import android.content.ClipboardManager
import android.text.Editable
import android.text.TextWatcher
import android.view.KeyEvent
import android.view.View
import android.view.inputmethod.EditorInfo
import android.widget.Button
import android.widget.EditText
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import io.github.leukoplast1980.mecrisppocket.R
import io.github.leukoplast1980.mecrisppocket.forth.Dictionary
import io.github.leukoplast1980.mecrisppocket.forth.History
import io.github.leukoplast1980.mecrisppocket.vm.VmHost
import java.io.File
import java.nio.ByteBuffer
import java.nio.CharBuffer
import java.nio.charset.CodingErrorAction

class ConsoleController(
    private val activity: Activity,
    private val vm: VmHost,
    /** Input text or cursor changed: update the suggestion strip. */
    private val onInputChanged: () -> Unit,
    /** The dictionary was re-read: completion and highlighting can update. */
    private val onDictionaryChanged: () -> Unit,
) : InputTarget {

    private val output: TextView = activity.findViewById(R.id.output)
    private val scroll: ScrollView = activity.findViewById(R.id.scroll)
    val input: EditText = activity.findViewById(R.id.input)
    val rawInput: RawInputView = activity.findViewById(R.id.rawInput)
    private val sendButton: Button = activity.findViewById(R.id.send)

    val history = History(File(activity.filesDir, "history.txt"))

    var rawMode = false
        private set

    /** What has been typed on the current line in raw mode (for suggestions). */
    private val rawLine = StringBuilder()

    private val readBuffer = ByteArray(4096)
    private val decoder = Charsets.UTF_8.newDecoder()
        .onMalformedInput(CodingErrorAction.REPLACE)
        .onUnmappableCharacter(CodingErrorAction.REPLACE)
    private val pending = ByteBuffer.allocate(8192)
    private val chars = CharBuffer.allocate(8192)

    /** Keep showing the newest output; off while the user reads further up. */
    private var followOutput = true

    /** Set when Forth may have changed its dictionary; re-read when it is idle. */
    private var dictionaryStale = true
    private var dictionaryLoading = false

    init {
        output.setText("", TextView.BufferType.EDITABLE)

        // New output (after layout, when its height is known) and a smaller
        // console (keyboard opening) keep the end in view while following.
        val follow = View.OnLayoutChangeListener { _, _, _, _, _, _, _, _, _ ->
            if (followOutput) scroll.scrollTo(0, output.bottom)
        }
        // A tap on the output gives the focus back to the input line; a long
        // press still selects text to copy.
        output.setOnFocusChangeListener { _, hasFocus ->
            if (hasFocus) output.post { if (!output.hasSelection()) focusInput() }
        }
        output.addOnLayoutChangeListener(follow)
        scroll.addOnLayoutChangeListener(follow)
        scroll.setOnScrollChangeListener { _, _, _, _, _ ->
            if (scroll.isLaidOut && output.isLaidOut) followOutput = atEnd()
        }

        sendButton.setOnClickListener { sendLine() }
        input.setOnEditorActionListener { _, actionId, event ->
            val enter = event != null && event.keyCode == KeyEvent.KEYCODE_ENTER &&
                event.action == KeyEvent.ACTION_DOWN
            if (actionId == EditorInfo.IME_ACTION_SEND || enter) { sendLine(); true } else false
        }
        input.setOnKeyListener { _, keyCode, event ->
            if (event.action != KeyEvent.ACTION_DOWN) return@setOnKeyListener false
            when (keyCode) {
                KeyEvent.KEYCODE_DPAD_UP -> { recall(-1); true }
                KeyEvent.KEYCODE_DPAD_DOWN -> { recall(1); true }
                KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER -> { sendLine(); true }
                else -> false
            }
        }
        input.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: Editable?) = onInputChanged()
        })

        rawInput.onText = { text -> sendRaw(text) }
        rawInput.onSpecialKey = { event -> rawSpecialKey(event) }
    }

    // ------------------------------------------------------------------
    // Output

    /** Moves Forth's output into the console. Call regularly on the UI thread. */
    fun poll() {
        var gotAny = false
        while (true) {
            val n = vm.read(readBuffer)
            if (n <= 0) break
            gotAny = true
            var off = 0
            while (off < n) {
                val take = minOf(n - off, pending.remaining())
                pending.put(readBuffer, off, take)
                off += take
                pending.flip()
                decoder.decode(pending, chars, false)
                pending.compact()
                chars.flip()
                appendText(chars.toString())
                chars.clear()
            }
        }
        if (gotAny && followOutput) scroll.post { scroll.scrollTo(0, output.bottom) }

        if (dictionaryStale && !dictionaryLoading && vm.waitingForKey()) {
            dictionaryStale = false
            dictionaryLoading = true
            Thread {
                vm.refreshDictionary()
                activity.runOnUiThread {
                    dictionaryLoading = false
                    onDictionaryChanged()
                }
            }.start()
        }
    }

    /** Appends Forth output; handles backspace (8) and drops carriage returns. */
    fun appendText(text: String) {
        val buf = output.editableText ?: return
        var start = 0
        for (i in text.indices) {
            val c = text[i]
            if (c == '\b' || c == '\r') {
                if (i > start) buf.append(text, start, i)
                if (c == '\b' && buf.isNotEmpty() && buf[buf.length - 1] != '\n') {
                    buf.delete(buf.length - 1, buf.length)
                }
                start = i + 1
            }
        }
        if (start < text.length) buf.append(text, start, text.length)
        if (buf.length > MAX_CHARS) buf.delete(0, buf.length - MAX_CHARS * 3 / 4)
    }

    fun clear() {
        output.editableText?.clear()
    }

    fun copyAll() {
        val clip = activity.getSystemService(ClipboardManager::class.java) ?: return
        clip.setPrimaryClip(ClipData.newPlainText("Mecrisp Pocket", output.text))
        Toast.makeText(activity, R.string.copied, Toast.LENGTH_SHORT).show()
    }

    /** The end of the console, for saving between runs. */
    fun savedText(): CharSequence {
        val t = output.text
        return t.subSequence(maxOf(0, t.length - SAVED_CHARS), t.length)
    }

    /**
     * Shows the end of the output and keeps following it. Not
     * fullScroll(FOCUS_DOWN): that also takes the focus from the input line.
     */
    fun scrollToEnd() {
        followOutput = true
        scroll.post { scroll.scrollTo(0, output.bottom) }
    }

    private fun atEnd() = scroll.scrollY + scroll.height - scroll.paddingBottom >=
        output.bottom - output.lineHeight

    // ------------------------------------------------------------------
    // Input

    fun markDictionaryStale() {
        dictionaryStale = true
    }

    fun setRawMode(raw: Boolean) {
        rawMode = raw
        rawLine.setLength(0)
        input.visibility = if (raw) View.GONE else View.VISIBLE
        sendButton.visibility = if (raw) View.GONE else View.VISIBLE
        rawInput.visibility = if (raw) View.VISIBLE else View.GONE
        focusInput()
        onInputChanged()
    }

    fun focusInput() {
        if (rawMode) rawInput.showKeyboard() else input.requestFocus()
    }

    fun hasFocus(): Boolean = input.hasFocus() || rawInput.hasFocus()

    private fun sendLine() {
        val line = input.text.toString()
        input.text.clear()
        history.add(line)
        send(line + "\n")
        markDictionaryStale()
        scrollToEnd()
        input.requestFocus()
    }

    private fun send(text: String) {
        if (!vm.send(text)) Toast.makeText(activity, R.string.input_full, Toast.LENGTH_SHORT).show()
    }

    private fun recall(direction: Int) {
        val text = history.move(direction)
        input.setText(text)
        input.setSelection(text.length)
    }

    private fun sendRaw(text: String) {
        send(text)
        rawLine.append(text)
        onInputChanged()
    }

    private fun rawSpecialKey(event: KeyEvent): Boolean {
        if (event.action != KeyEvent.ACTION_DOWN) return false
        when (event.keyCode) {
            KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER -> {
                send("\r")
                scrollToEnd()
                if (rawLine.isNotBlank()) history.add(rawLine.toString())
                rawLine.setLength(0)
                markDictionaryStale()
            }
            KeyEvent.KEYCODE_DEL -> {
                send("\b")
                if (rawLine.isNotEmpty()) rawLine.setLength(rawLine.length - 1)
            }
            KeyEvent.KEYCODE_TAB -> Completion.tab(this)
            else -> return false
        }
        onInputChanged()
        return true
    }

    // ------------------------------------------------------------------
    // InputTarget

    override fun insertText(text: String) {
        if (rawMode) {
            sendRaw(text)
        } else {
            val start = input.selectionStart.coerceAtLeast(0)
            val end = input.selectionEnd.coerceAtLeast(0)
            input.text.replace(minOf(start, end), maxOf(start, end), text)
            input.requestFocus()
        }
    }

    override fun currentToken(): String {
        val text: CharSequence
        val end: Int
        if (rawMode) {
            text = rawLine
            end = rawLine.length
        } else {
            text = input.text
            end = input.selectionStart.coerceIn(0, text.length)
        }
        var start = end
        while (start > 0 && !text[start - 1].isWhitespace()) start--
        return text.subSequence(start, end).toString()
    }

    override fun applyCompletion(token: String, word: String, finished: Boolean) {
        val tail = if (finished) " " else ""
        if (rawMode) {
            sendRaw((if (word.length >= token.length) word.substring(token.length) else "") + tail)
        } else {
            val end = input.selectionStart.coerceIn(0, input.text.length)
            val start = (end - token.length).coerceAtLeast(0)
            input.text.replace(start, end, word + tail)
            input.setSelection(start + word.length + tail.length)
        }
        focusInput()
    }

    override fun completionDictionary(): Dictionary = vm.dictionary

    override fun arrow(keyCode: Int) {
        if (rawMode) {
            if (keyCode == KeyEvent.KEYCODE_DPAD_LEFT) rawSpecialKey(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_DEL))
            return
        }
        when (keyCode) {
            KeyEvent.KEYCODE_DPAD_UP -> recall(-1)
            KeyEvent.KEYCODE_DPAD_DOWN -> recall(1)
            else -> {
                val delta = if (keyCode == KeyEvent.KEYCODE_DPAD_LEFT) -1 else 1
                input.setSelection((input.selectionStart + delta).coerceIn(0, input.text.length))
                input.requestFocus()
            }
        }
    }

    companion object {
        private const val MAX_CHARS = 200_000
        private const val SAVED_CHARS = 20_000
    }
}
