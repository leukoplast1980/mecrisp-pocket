package io.github.leukoplast1980.mecrisppocket

import android.os.SystemClock
import android.util.Log
import android.view.MotionEvent
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.google.android.material.tabs.TabLayout
import io.github.leukoplast1980.mecrisppocket.ui.ForthEditor
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** The editor keeps gliding after a fling, and a tap while it glides only stops it. */
@RunWith(AndroidJUnit4::class)
class EditorFlingProbeTest {

    private val instr = InstrumentationRegistry.getInstrumentation()

    /** A quick upward swipe over the middle of the editor, in screen coordinates. */
    private fun swipeUp(x: Float, top: Float, height: Float) {
        val t0 = SystemClock.uptimeMillis()
        val y0 = top + height * 0.8f
        val y1 = top + height * 0.2f
        instr.sendPointerSync(MotionEvent.obtain(t0, t0, MotionEvent.ACTION_DOWN, x, y0, 0))
        for (i in 1..5) {
            val y = y0 + (y1 - y0) * i / 5
            instr.sendPointerSync(MotionEvent.obtain(t0, t0 + i * 10L, MotionEvent.ACTION_MOVE, x, y, 0))
        }
        instr.sendPointerSync(MotionEvent.obtain(t0, t0 + 60, MotionEvent.ACTION_UP, x, y1, 0))
    }

    private fun tap(x: Float, y: Float) {
        val t0 = SystemClock.uptimeMillis()
        instr.sendPointerSync(MotionEvent.obtain(t0, t0, MotionEvent.ACTION_DOWN, x, y, 0))
        instr.sendPointerSync(MotionEvent.obtain(t0, t0 + 40, MotionEvent.ACTION_UP, x, y, 0))
    }

    @Test
    fun glide() {
        val scenario = ActivityScenario.launch(MainActivity::class.java)
        Thread.sleep(2500)
        scenario.onActivity { a ->
            val tabs = a.findViewById<TabLayout>(R.id.tabs)
            for (i in 0 until tabs.tabCount) if (tabs.getTabAt(i)?.text == "Editor") tabs.getTabAt(i)?.select()
        }
        Thread.sleep(800)

        lateinit var editor: ForthEditor
        val loc = IntArray(2)
        scenario.onActivity { a ->
            editor = a.findViewById(R.id.editor)
            editor.setText((1..400).joinToString("\n") { "\\ line $it: some text to scroll through" })
            editor.setSelection(0)
            editor.scrollTo(0, 0)
            editor.getLocationOnScreen(loc)
        }
        Thread.sleep(500)
        val x = loc[0] + editor.width * 0.6f
        val top = loc[1].toFloat()
        val h = editor.height.toFloat()

        // A fling keeps the text moving after the finger is gone.
        swipeUp(x, top, h)
        var afterUp = 0
        scenario.onActivity { afterUp = editor.scrollY }
        Thread.sleep(300)
        var later = 0
        scenario.onActivity { later = editor.scrollY }
        Log.i("UiProbe", "fling: scrollY after up $afterUp, 300 ms later $later")
        assertTrue("no glide: $afterUp then $later", later > afterUp + 50)

        // A tap during the glide stops it and leaves the cursor alone.
        Thread.sleep(1500)
        swipeUp(x, top, h)
        Thread.sleep(80)
        var cursorBefore = 0
        scenario.onActivity { cursorBefore = editor.selectionStart }
        tap(x, top + h / 2)
        var stopped = 0
        scenario.onActivity { stopped = editor.scrollY }
        Thread.sleep(300)
        var still = 0
        var cursorAfter = 0
        scenario.onActivity { still = editor.scrollY; cursorAfter = editor.selectionStart }
        Log.i("UiProbe", "tap: scrollY $stopped then $still, cursor $cursorBefore then $cursorAfter")
        assertTrue("glide went on after the tap: $stopped then $still", still - stopped < 5)
        assertEquals("the tap moved the cursor", cursorBefore, cursorAfter)
    }
}
