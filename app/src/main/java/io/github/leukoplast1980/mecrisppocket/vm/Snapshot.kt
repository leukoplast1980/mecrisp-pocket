/*
 * Mecrisp Pocket - one consistent view of the CPU state.
 * Copyright (C) 2026 The Mecrisp Pocket authors. GPL-3.0-or-later.
 */
package io.github.leukoplast1980.mecrisppocket.vm

class Snapshot(raw: IntArray) {
    val pc = raw[0]
    val x = raw.copyOfRange(1, 33)
    val mstatus = raw[33]
    val mie = raw[34]
    val mip = raw[35]
    val mtvec = raw[36]
    val mepc = raw[37]
    val mcause = raw[38]
    val mtval = raw[39]
    val mscratch = raw[40]
    val instret = (raw[42].toLong() shl 32) or (raw[41].toLong() and 0xffffffffL)
    val mtime = (raw[44].toLong() shl 32) or (raw[43].toLong() and 0xffffffffL)
    val waitingForKey = raw[45] != 0
    val paused = raw[46] != 0
}
