/*
 * Mecrisp Pocket - facts about a saved flash image.
 * Copyright (C) 2026 The Mecrisp Pocket authors. GPL-3.0-or-later.
 */
package io.github.leukoplast1980.mecrisppocket.vm

object FlashImage {
    /** Where the user dictionary starts; the core lies below (docs/board.md). */
    const val USER_START = 0x8000

    /** True if nothing was compiled into flash: the user area is all erased ($FF). */
    fun userAreaEmpty(image: ByteArray): Boolean {
        for (i in USER_START until image.size) if (image[i] != 0xFF.toByte()) return false
        return true
    }
}
