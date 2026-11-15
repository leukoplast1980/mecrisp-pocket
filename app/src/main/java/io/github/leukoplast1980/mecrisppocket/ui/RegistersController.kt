/*
 * Mecrisp Pocket - the Registers view: CPU, Forth stacks and device
 * registers, refreshed while visible, with Pause and Step.
 * Copyright (C) 2026 The Mecrisp Pocket authors. GPL-3.0-or-later.
 */
package io.github.leukoplast1980.mecrisppocket.ui

import android.app.Activity
import android.os.SystemClock
import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.widget.Button
import android.widget.TextView
import io.github.leukoplast1980.mecrisppocket.R
import io.github.leukoplast1980.mecrisppocket.vm.Snapshot
import io.github.leukoplast1980.mecrisppocket.vm.VmHost
import java.nio.ByteBuffer
import java.nio.ByteOrder

class RegistersController(private val activity: Activity, private val vm: VmHost) {

    private val text: TextView = activity.findViewById(R.id.registers)
    private val runState: TextView = activity.findViewById(R.id.runState)
    private val pauseButton: Button = activity.findViewById(R.id.pause)
    private val stepButton: Button = activity.findViewById(R.id.step)

    private val changedColor = activity.getColor(R.color.changed)
    private val labelColor = activity.getColor(R.color.label)
    private val accentColor = activity.getColor(R.color.accent)

    private val dataStackBase = vm.symbols.getValue("datenstackanfang")
    private val returnStackBase = vm.symbols.getValue("returnstackanfang")

    /** Values shown last time. */
    private var previous = HashMap<String, Long>()

    /** When each value last changed (uptime ms), for highlighting while running. */
    private val changedAt = HashMap<String, Long>()

    /** Values before the last Step: while paused, changes against these stay highlighted. */
    private var stepBaseline: Map<String, Long>? = null

    private var now = 0L

    /** Called after Pause or Step so the console can update its dictionary. */
    var onPausedChanged: ((Boolean) -> Unit)? = null

    init {
        pauseButton.setOnClickListener {
            val paused = !vm.isUserPaused
            vm.setUserPaused(paused)
            stepBaseline = null
            if (paused) Thread { vm.refreshDictionary() }.start()
            updateButtons()
            onPausedChanged?.invoke(paused)
        }
        stepButton.setOnClickListener {
            stepBaseline = HashMap(previous)
            vm.step()
            text.postDelayed({ refresh() }, 20)
        }
        updateButtons()
    }

    private fun updateButtons() {
        val paused = vm.isUserPaused
        pauseButton.setText(if (paused) R.string.resume else R.string.pause)
        stepButton.isEnabled = paused
    }

    fun refresh() {
        now = SystemClock.uptimeMillis()
        val s = vm.snapshot()
        val devices = vm.deviceRegs(DEVICE_REGS.map { it.second }.toIntArray())
        val current = HashMap<String, Long>()
        val b = SpannableStringBuilder()

        runState.text = when {
            s.paused -> "paused"
            s.waitingForKey -> "waiting for input"
            else -> "running"
        } + "  ·  %,d instructions  ·  %.1f s".format(s.instret, s.mtime / 1e6)

        // Where the CPU is
        val word = vm.dictionary.wordAt(s.pc)
        val where = if (word != null) "${word.name} +${s.pc - word.code}" else ""
        heading(b, "Program counter")
        value(b, current, "pc", "pc", s.pc.toLong() and MASK, where)

        // Forth view of the registers
        heading(b, "Forth")
        val psp = s.x[9]
        val depth = (dataStackBase - psp) / 4
        val rdepth = (returnStackBase - s.x[2]) / 4
        value(b, current, "TOS", "TOS   x8", s.x[8].toLong() and MASK, s.x[8].toString())
        value(b, current, "PSP", "PSP   x9", psp.toLong() and MASK, "depth $depth")
        value(b, current, "RSP", "RSP   x2", s.x[2].toLong() and MASK, "depth $rdepth")
        value(b, current, "I", "I     x3", s.x[3].toLong() and MASK, s.x[3].toString())
        value(b, current, "limit", "limit x4", s.x[4].toLong() and MASK, s.x[4].toString())

        // Data stack contents
        heading(b, "Data stack")
        when {
            depth <= 0 -> b.append(if (depth == 0) "  empty\n" else "  underflow ($depth)\n")
            else -> {
                val shown = minOf(depth, MAX_STACK)
                value(b, current, "ds0", "  TOS", s.x[8].toLong() and MASK, s.x[8].toString())
                if (shown > 1) {
                    val mem = vm.readMemory(psp, (shown - 1) * 4)
                    if (mem != null) {
                        val bb = ByteBuffer.wrap(mem).order(ByteOrder.LITTLE_ENDIAN)
                        for (i in 1 until shown) {
                            val v = bb.getInt((i - 1) * 4)
                            value(b, current, "ds$i", "  %-3d".format(i), v.toLong() and MASK, v.toString())
                        }
                    }
                }
                if (depth > shown) b.append("  … ${depth - shown} more\n")
            }
        }

        // All CPU registers, two per line
        heading(b, "CPU registers")
        for (r in 0 until 32 step 2) {
            pair(b, current, "x$r", s.x[r], "x${r + 1}", s.x[r + 1])
        }

        heading(b, "Control registers")
        value(b, current, "mstatus", "mstatus ", s.mstatus.toLong() and MASK, statusBits(s.mstatus))
        value(b, current, "mie", "mie     ", s.mie.toLong() and MASK, irqBits(s.mie))
        value(b, current, "mip", "mip     ", s.mip.toLong() and MASK, irqBits(s.mip))
        value(b, current, "mtvec", "mtvec   ", s.mtvec.toLong() and MASK, "")
        value(b, current, "mepc", "mepc    ", s.mepc.toLong() and MASK, vm.dictionary.wordAt(s.mepc)?.name ?: "")
        value(b, current, "mcause", "mcause  ", s.mcause.toLong() and MASK, causeText(s.mcause))
        value(b, current, "mtval", "mtval   ", s.mtval.toLong() and MASK, "")
        value(b, current, "mscratch", "mscratch", s.mscratch.toLong() and MASK, "")

        heading(b, "Devices")
        for ((i, reg) in DEVICE_REGS.withIndex()) {
            val v = devices.getOrElse(i) { 0 }
            value(b, current, reg.first, "%-16s".format(reg.first), v.toLong() and MASK, deviceNote(reg.first, v))
        }

        previous = current
        text.text = b
        updateButtons()
    }

    private fun heading(b: SpannableStringBuilder, title: String) {
        if (b.isNotEmpty()) b.append('\n')
        val start = b.length
        b.append(title).append('\n')
        b.setSpan(ForegroundColorSpan(accentColor), start, b.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    private fun value(
        b: SpannableStringBuilder, current: MutableMap<String, Long>,
        key: String, label: String, v: Long, note: String,
    ) {
        current[key] = v
        val start = b.length
        b.append(label)
        b.setSpan(ForegroundColorSpan(labelColor), start, b.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        b.append("  ")
        hex(b, key, v)
        if (note.isNotEmpty()) b.append("  ").append(note)
        b.append('\n')
    }

    private fun pair(b: SpannableStringBuilder, current: MutableMap<String, Long>, k1: String, v1: Int, k2: String, v2: Int) {
        for ((k, v) in listOf(k1 to v1, k2 to v2)) {
            val lv = v.toLong() and MASK
            current[k] = lv
            val start = b.length
            b.append("%-4s".format(k))
            b.setSpan(ForegroundColorSpan(labelColor), start, b.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            hex(b, k, lv)
            b.append("   ")
        }
        b.append('\n')
    }

    private fun hex(b: SpannableStringBuilder, key: String, v: Long) {
        val start = b.length
        b.append("%08X".format(v))
        if (highlighted(key, v)) {
            b.setSpan(ForegroundColorSpan(changedColor), start, b.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
    }

    /**
     * Paused after a Step: everything the step changed stays highlighted
     * until the next Step. Running: a value stays highlighted for
     * HOLD_MS after it last changed, so it is readable.
     */
    private fun highlighted(key: String, v: Long): Boolean {
        val old = previous[key]
        if (old != null && old != v) changedAt[key] = now
        val baseline = stepBaseline
        if (vm.isUserPaused && baseline != null) {
            val before = baseline[key]
            return before != null && before != v
        }
        val t = changedAt[key] ?: return false
        return now - t < HOLD_MS
    }

    private fun statusBits(v: Int): String = buildList {
        if ((v and 0x8) != 0) add("MIE")
        if ((v and 0x80) != 0) add("MPIE")
    }.joinToString(" ")

    private fun irqBits(v: Int): String = buildList {
        if ((v and (1 shl 11)) != 0) add("external")
        if ((v and (1 shl 7)) != 0) add("timer")
        if ((v and (1 shl 3)) != 0) add("software")
    }.joinToString(" ")

    private fun causeText(v: Int): String {
        if (v == 0) return ""
        val code = v and 0x7fffffff
        return if (v < 0) {
            when (code) { 3 -> "software irq"; 7 -> "timer irq"; 11 -> "external irq"; else -> "irq $code" }
        } else {
            when (code) {
                0 -> "misaligned jump"; 1 -> "bad fetch address"; 2 -> "illegal instruction"
                3 -> "ebreak"; 5 -> "bad load address"; 7 -> "bad store address"
                11 -> "ecall"; else -> "exception $code"
            }
        }
    }

    private fun deviceNote(name: String, v: Int): String = when (name) {
        "UART FLAGS" -> buildList {
            if ((v and 0x100) != 0) add("byte received")
            if ((v and 0x200) != 0) add("output full")
        }.joinToString(", ")
        "SYS RESET CAUSE" -> when (v) { 0 -> "power-on"; 1 -> "Reset button"; 2 -> "software"; else -> "" }
        "IRQ PENDING", "IRQ ENABLE" -> buildList {
            if ((v and 1) != 0) add("break")
            if ((v and 2) != 0) add("uart")
            if ((v and 4) != 0) add("audio")
            if ((v and 8) != 0) add("gpio")
        }.joinToString(" ")
        else -> ""
    }

    companion object {
        private const val MASK = 0xffffffffL
        private const val MAX_STACK = 16
        private const val HOLD_MS = 1500L

        private val DEVICE_REGS = listOf(
            "UART FLAGS" to 0x40020000,
            "FLASH ADDR" to 0x40030000,
            "SYS RESET CAUSE" to 0x40040008,
            "SYS SAFE BOOT" to 0x40040010,
            "IRQ PENDING" to 0x40050000,
            "IRQ ENABLE" to 0x40050004,
            "MTIME LO" to 0x40060000,
            "MTIME HI" to 0x40060004,
            "MTIMECMP LO" to 0x40060008,
            "MTIMECMP HI" to 0x4006000C,
        )
    }
}
