package io.github.leukoplast1980.mecrisppocket.forth

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ConsoleStyleTest {

    private fun styled(line: String, from: Int = 0) =
        ConsoleStyle.messages(line, from).map { line.substring(it.start, it.end) + ":" + it.kind }

    @Test fun okPrompt() {
        assertEquals(listOf("ok.:OK"), styled("2 3 + . 5  ok.", 7))
    }

    @Test fun notFoundIncludesTheWord() {
        assertEquals(listOf("frob not found.:ERROR"), styled("1 frob frob not found.", 7))
    }

    @Test fun echoIsSkipped() {
        // The " ok." typed inside the string is part of the echo, not the prompt.
        val line = """: x ." ok." ;  ok."""
        val ranges = ConsoleStyle.messages(line, 13)
        assertEquals(listOf(MessageRange(15, 18, MessageKind.OK)), ranges)
    }

    @Test fun coreErrors() {
        assertEquals(listOf("Stack underflow:ERROR"), styled("drop drop Stack underflow", 10))
        assertEquals(listOf("Stack not balanced.:ERROR"), styled("; Stack not balanced.", 1))
        assertEquals(listOf("Exception mcause: 00000005 mepc: 00008124 !:ERROR"),
            styled("Exception mcause: 00000005 mepc: 00008124 !"))
    }

    @Test fun warnings() {
        assertEquals(listOf("Redefine dup.:WARNING", "ok.:OK"), styled(": dup ; Redefine dup.  ok.", 8))
        assertEquals(listOf("Stop.:WARNING"), styled(" Stop."))
    }

    @Test fun plainOutputIsNotColoured() {
        assertEquals(emptyList<String>(), styled("Hello Pocket"))
        assertEquals(emptyList<String>(), styled("cookie"))  // "ok" inside a word
    }

    @Test fun echo() {
        assertTrue(ConsoleStyle.startsWithEcho("2 3 + . 5  ok.", "2 3 + ."))
        assertTrue(ConsoleStyle.startsWithEcho("  ok.", ""))
        assertFalse(ConsoleStyle.startsWithEcho("2 3 + ", "2 3 +."))
        assertFalse(ConsoleStyle.startsWithEcho("12 ok.", "1"))
        assertFalse(ConsoleStyle.startsWithEcho("2 3", "2 3"))  // no space yet: not sure
    }
}
