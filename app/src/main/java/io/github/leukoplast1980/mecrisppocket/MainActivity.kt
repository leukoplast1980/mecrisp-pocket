/*
 * Mecrisp Pocket - minimal console (milestone 1).
 * Copyright (C) 2026 The Mecrisp Pocket authors. GPL-3.0-or-later.
 */
package io.github.leukoplast1980.mecrisppocket

import android.app.Activity
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.KeyEvent
import android.view.View
import android.view.WindowInsets
import android.view.inputmethod.EditorInfo
import android.widget.Button
import android.widget.EditText
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import io.github.leukoplast1980.mecrisppocket.vm.VmHost
import java.nio.ByteBuffer
import java.nio.CharBuffer
import java.nio.charset.CodingErrorAction

class MainActivity : Activity() {

    private lateinit var vm: VmHost
    private lateinit var output: TextView
    private lateinit var scroll: ScrollView
    private lateinit var input: EditText

    private val handler = Handler(Looper.getMainLooper())
    private val readBuffer = ByteArray(4096)
    private val decoder = Charsets.UTF_8.newDecoder()
        .onMalformedInput(CodingErrorAction.REPLACE)
        .onUnmappableCharacter(CodingErrorAction.REPLACE)
    private val pending = ByteBuffer.allocate(8192)
    private val chars = CharBuffer.allocate(8192)
    private val history = ArrayList<String>()
    private var historyPos = 0

    private val poll = object : Runnable {
        override fun run() {
            drainOutput()
            handler.postDelayed(this, POLL_MS)
        }
    }

    private val autosave = object : Runnable {
        override fun run() {
            Thread { vm.saveFlash() }.start()
            handler.postDelayed(this, AUTOSAVE_MS)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        vm = VmHost.get(this)

        output = findViewById(R.id.output)
        scroll = findViewById(R.id.scroll)
        input = findViewById(R.id.input)

        savedInstanceState?.getCharSequence(KEY_OUTPUT)?.let { output.text = it }

        findViewById<Button>(R.id.stop).setOnClickListener { vm.stop() }
        findViewById<Button>(R.id.reset).apply {
            setOnClickListener { vm.reset(safeBoot = false) }
            setOnLongClickListener {
                vm.reset(safeBoot = true)
                Toast.makeText(this@MainActivity, R.string.safe_boot, Toast.LENGTH_SHORT).show()
                true
            }
        }
        findViewById<Button>(R.id.send).setOnClickListener { sendInput() }

        input.setOnEditorActionListener { _, actionId, event ->
            val enter = event != null && event.keyCode == KeyEvent.KEYCODE_ENTER &&
                event.action == KeyEvent.ACTION_DOWN
            if (actionId == EditorInfo.IME_ACTION_SEND || enter) {
                sendInput()
                true
            } else {
                false
            }
        }
        input.setOnKeyListener { _, keyCode, event ->
            if (event.action != KeyEvent.ACTION_DOWN) return@setOnKeyListener false
            when (keyCode) {
                KeyEvent.KEYCODE_DPAD_UP -> { recall(-1); true }
                KeyEvent.KEYCODE_DPAD_DOWN -> { recall(1); true }
                else -> false
            }
        }

        applyInsets(findViewById(R.id.root))

        if (vm.coreUpdateAvailable) appendText(getString(R.string.core_update_notice))
    }

    private fun applyInsets(root: View) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            root.setOnApplyWindowInsetsListener { v, insets ->
                val bars = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.ime())
                v.setPadding(bars.left, bars.top, bars.right, bars.bottom)
                WindowInsets.CONSUMED
            }
        } else {
            root.fitsSystemWindows = true
        }
    }

    override fun onStart() {
        super.onStart()
        vm.setRunning(true)
        handler.post(poll)
        handler.postDelayed(autosave, AUTOSAVE_MS)
    }

    override fun onStop() {
        handler.removeCallbacks(poll)
        handler.removeCallbacks(autosave)
        vm.setRunning(false)
        Thread { vm.saveFlash() }.start()
        super.onStop()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        // Keep the saved state small: Android limits its size.
        val text = output.text
        outState.putCharSequence(KEY_OUTPUT, text.subSequence(maxOf(0, text.length - 20_000), text.length))
    }

    private fun sendInput() {
        val line = input.text.toString()
        input.text.clear()
        if (line.isNotBlank() && history.lastOrNull() != line) history.add(line)
        historyPos = history.size
        if (!vm.send(line + "\n")) {
            Toast.makeText(this, R.string.input_full, Toast.LENGTH_SHORT).show()
        }
    }

    private fun recall(direction: Int) {
        if (history.isEmpty()) return
        historyPos = (historyPos + direction).coerceIn(0, history.size)
        val text = if (historyPos < history.size) history[historyPos] else ""
        input.setText(text)
        input.setSelection(text.length)
    }

    private fun drainOutput() {
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
                appendText(chars.toString().replace("\r", ""))
                chars.clear()
            }
        }
        if (gotAny) scrollToEnd()
    }

    private fun appendText(text: String) {
        output.append(text)
        val len = output.text.length
        if (len > MAX_CHARS) {
            output.text = output.text.subSequence(len - MAX_CHARS * 3 / 4, len)
        }
    }

    private fun scrollToEnd() {
        scroll.post { scroll.fullScroll(View.FOCUS_DOWN); input.requestFocus() }
    }

    companion object {
        private const val POLL_MS = 30L
        private const val AUTOSAVE_MS = 5000L
        private const val MAX_CHARS = 200_000
        private const val KEY_OUTPUT = "output"
    }
}
