package io.github.leukoplast1980.mecrisppocket.vm

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class FlashImageTest {

    private fun image() = ByteArray(256 * 1024) { 0xFF.toByte() }.also { it[0] = 0x6f; it[0x7fff] = 0 }

    @Test fun coreOnlyIsEmpty() {
        assertTrue(FlashImage.userAreaEmpty(image()))
    }

    @Test fun aCompiledWordIsNotEmpty() {
        val img = image()
        img[0x8000 + 40] = 0x12
        assertFalse(FlashImage.userAreaEmpty(img))
    }

    @Test fun lastByteCounts() {
        val img = image()
        img[img.size - 1] = 0
        assertFalse(FlashImage.userAreaEmpty(img))
    }
}
