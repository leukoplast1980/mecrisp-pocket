/*
 * Mecrisp Pocket - the REPL: output, line and raw input, completion
 * suggestions, extra keys and history.
 * Copyright (C) 2026 The Mecrisp Pocket authors. GPL-3.0-or-later.
 */
package io.github.leukoplast1980.mecrisppocket.ui

import android.app.Activity
import android.content.ClipData
import android.content.ClipboardManager
import android.text.Editable
import android.text.TextWatcher
import android.view.KeyEvent
import android.view.LayoutInflater
import android.view.View
import android.view.inputmethod.EditorInfo
import android.widget.Button
import android.widget.EditText
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import io.github.leukoplast1980.mecrisppocket.R
import io.github.leukoplast1980.mecrisppocket.forth.Dictionary
import io.github.leukoplast1980.mecrisppocket.forth.History
import io.github.leukoplast1980.mecrisppocket.forth.Word
import io.github.leukoplast1980.mecrisppocket.vm.VmHost
import java.io.File
import java.nio.ByteBuffer
import java.nio.CharBuffer
import java.nio.charset.CodingErrorAction

class ConsoleController(private val activity: Activity, private val vm: VmHost) {

    private val output: TextView = activity.findViewById(R.id.output)
    private val scroll: ScrollView = activity.findViewById(R.id.scroll)
    private val input: EditText = activity.findViewById(R.id.input)
    private val rawInput: RawInputView = activity.findViewById(R.id.rawInput)
    private val sendButton: Button = activity.findViewById(R.id.send)
    private val suggestionScroll: HorizontalScrollView = activity.findViewById(R.id.suggestionScroll)
    private val suggestions: LinearLayout = activity.findViewById(R.id.suggestions)
    private val extraKeys: LinearLayout = activity.findViewById(R.id.extraKeys)

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

    /** Set when Forth may have changed its dictionary; re-read when it is idle. */
    private var dictionaryStale = true
    private var dictionaryLoading = false

    init {
        output.setText("", TextView.BufferType.EDITABLE)

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
            override fun afterTextChanged(s: Editable?) = updateSuggestions()
        })

        rawInput.onText = { text -> sendRaw(text) }
        rawInput.onSpecialKey = { event -> rawSpecialKey(event) }

        buildExtraKeys()
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
        if (gotAny) scrollToEnd()

        if (dictionaryStale && !dictionaryLoading && vm.waitingForKey()) {
            dictionaryStale = false
            dictionaryLoading = true
            Thread {
                vm.refreshDictionary()
                activity.runOnUiThread {
                    dictionaryLoading = false
                    updateSuggestions()
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

    /** Last part of the console, for saving across activity recreation. */
    fun savedText(): CharSequence {
        val t = output.text
        return t.subSequence(maxOf(0, t.length - 20_000), t.length)
    }

    private fun scrollToEnd() {
        scroll.post { scroll.fullScroll(View.FOCUS_DOWN) }
    }

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
        if (raw) rawInput.showKeyboard() else input.requestFocus()
        updateSuggestions()
    }

    fun focusInput() {
        if (rawMode) rawInput.showKeyboard() else input.requestFocus()
    }

    private fun sendLine() {
        val line = input.text.toString()
        input.text.clear()
        history.add(line)
        send(line + "\n")
        markDictionaryStale()
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
        updateSuggestions()
    }

    private fun rawSpecialKey(event: KeyEvent): Boolean {
        if (event.action != KeyEvent.ACTION_DOWN) return false
        when (event.keyCode) {
            KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER -> {
                send("\r")
                if (rawLine.isNotBlank()) history.add(rawLine.toString())
                rawLine.setLength(0)
                markDictionaryStale()
            }
            KeyEvent.KEYCODE_DEL -> {
                send("\b")
                if (rawLine.isNotEmpty()) rawLine.setLength(rawLine.length - 1)
            }
            KeyEvent.KEYCODE_TAB -> complete()
            else -> return false
        }
        updateSuggestions()
        return true
    }

    // ------------------------------------------------------------------
    // Completion

    /** The partial word before the cursor. */
    private fun currentToken(): String {
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

    private fun updateSuggestions() {
        val token = currentToken()
        val matches = vm.dictionary.complete(token)
        suggestions.removeAllViews()
        if (matches.isEmpty() || (matches.size == 1 && matches[0].name == token)) {
            suggestionScroll.visibility = View.GONE
            return
        }
        val inflater = LayoutInflater.from(activity)
        for (w in matches) {
            val chip = inflater.inflate(R.layout.suggestion, suggestions, false) as TextView
            chip.text = w.name
            if (w.user) chip.setTextColor(activity.getColor(R.color.accent))
            chip.setOnClickListener { applyCompletion(token, w.name, true) }
            suggestions.addView(chip)
        }
        suggestionScroll.visibility = View.VISIBLE
        suggestionScroll.scrollTo(0, 0)
    }

    /** Tab: completes a unique match, or as far as all matches agree. */
    fun complete() {
        val token = currentToken()
        val matches = vm.dictionary.complete(token, limit = 1000)
        if (matches.isEmpty()) return
        if (matches.size == 1) {
            applyCompletion(token, matches[0].name, true)
        } else {
            val prefix = commonPrefix(matches)
            if (prefix.length > token.length) applyCompletion(token, prefix, false)
        }
    }

    private fun commonPrefix(words: List<Word>): String {
        var prefix = words[0].name
        for (w in words) {
            var n = 0
            while (n < prefix.length && n < w.name.length &&
                Dictionary.foldCase(prefix[n].toString()) == Dictionary.foldCase(w.name[n].toString())
            ) n++
            prefix = prefix.substring(0, n)
        }
        return prefix
    }

    private fun applyCompletion(token: String, word: String, finished: Boolean) {
        val suffix = (if (word.length >= token.length) word.substring(token.length) else "") +
            (if (finished) " " else "")
        if (rawMode) {
            sendRaw(suffix)
        } else {
            val end = input.selectionStart.coerceIn(0, input.text.length)
            val start = end - token.length
            input.text.replace(start, end, word + if (finished) " " else "")
            input.setSelection(start + word.length + if (finished) 1 else 0)
        }
        focusInput()
    }

    // ------------------------------------------------------------------
    // Extra keys

    private fun buildExtraKeys() {
        val inflater = LayoutInflater.from(activity)
        for (label in EXTRA_KEYS) {
            val key = inflater.inflate(R.layout.extra_key, extraKeys, false) as TextView
            key.text = label
            key.setOnClickListener { extraKey(label) }
            extraKeys.addView(key)
        }
    }

    private fun extraKey(label: String) {
        when (label) {
            "Tab" -> complete()
            "↑" -> if (!rawMode) recall(-1)
            "↓" -> if (!rawMode) recall(1)
            "←" -> if (!rawMode) moveCursor(-1) else rawSpecialKey(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_DEL))
            "→" -> if (!rawMode) moveCursor(1)
            else -> insert(label)
        }
    }

    private fun insert(text: String) {
        if (rawMode) {
            sendRaw(text)
        } else {
            val start = input.selectionStart.coerceAtLeast(0)
            val end = input.selectionEnd.coerceAtLeast(0)
            input.text.replace(minOf(start, end), maxOf(start, end), text)
            input.requestFocus()
        }
    }

    private fun moveCursor(delta: Int) {
        val pos = (input.selectionStart + delta).coerceIn(0, input.text.length)
        input.setSelection(pos)
        input.requestFocus()
    }

    companion object {
        private const val MAX_CHARS = 200_000
        private val EXTRA_KEYS = listOf(
            "Tab", ":", ";", "(", ")", "@", "!", ".", "\"", "'", "$", "#", "%",
            "←", "→", "↑", "↓",
        )
    }
}
