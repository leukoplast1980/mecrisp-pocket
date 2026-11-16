package io.github.leukoplast1980.mecrisppocket

import android.graphics.Bitmap
import android.os.SystemClock
import android.util.Log
import android.view.MotionEvent
import android.widget.EditText
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.google.android.material.tabs.TabLayout
import io.github.leukoplast1980.mecrisppocket.ui.ForthEditor
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.io.FileOutputStream

/** Probe: what a tap in the editor (and in the REPL input) looks like. */
@RunWith(AndroidJUnit4::class)
class EditorTapProbeTest {

    private val instr = InstrumentationRegistry.getInstrumentation()

    private fun tap(x: Float, y: Float) {
        val t0 = SystemClock.uptimeMillis()
        instr.sendPointerSync(MotionEvent.obtain(t0, t0, MotionEvent.ACTION_DOWN, x, y, 0))
        instr.sendPointerSync(MotionEvent.obtain(t0, t0 + 60, MotionEvent.ACTION_UP, x, y, 0))
    }

    private fun shot(name: String) {
        val bmp = instr.uiAutomation.takeScreenshot()
        val dir = instr.targetContext.getExternalFilesDir(null)!!
        FileOutputStream(File(dir, name)).use { bmp.compress(Bitmap.CompressFormat.PNG, 100, it) }
    }

    @Test
    fun tapLooks() {
        val scenario = ActivityScenario.launch(MainActivity::class.java)
        Thread.sleep(2500)

        // REPL input first, for comparison
        val loc = IntArray(2)
        lateinit var input: EditText
        scenario.onActivity { a ->
            input = a.findViewById(R.id.input)
            input.setText("1 2 + .")
            input.getLocationOnScreen(loc)
        }
        Thread.sleep(300)
        tap(loc[0] + 40f, loc[1] + input.height / 2f)
        Thread.sleep(700)
        tap(loc[0] + 70f, loc[1] + input.height / 2f)
        Thread.sleep(300)
        shot("tap-repl.png")

        scenario.onActivity { a ->
            val tabs = a.findViewById<TabLayout>(R.id.tabs)
            for (i in 0 until tabs.tabCount) if (tabs.getTabAt(i)?.text == "Editor") tabs.getTabAt(i)?.select()
        }
        Thread.sleep(800)
        lateinit var editor: ForthEditor
        scenario.onActivity { a ->
            editor = a.findViewById(R.id.editor)
            editor.setText((1..40).joinToString("\n") { ": word$it ( n -- n ) dup + ;" })
            editor.setSelection(0)
            editor.scrollTo(0, 0)
            editor.getLocationOnScreen(loc)
        }
        Thread.sleep(500)
        val x = loc[0] + editor.width * 0.5f
        val y = loc[1] + editor.height * 0.3f
        tap(x, y)
        Thread.sleep(300)
        shot("tap-editor-1st.png")
        var info = ""
        scenario.onActivity { info = "focused=${editor.isFocused} sel=${editor.selectionStart}..${editor.selectionEnd} cursorVisible=${editor.isCursorVisible}" }
        Log.i("UiProbe", "editor after 1st tap: $info")
        Thread.sleep(700)
        tap(x + 60f, y + 50f)
        Thread.sleep(300)
        shot("tap-editor-2nd.png")
        scenario.onActivity { info = "focused=${editor.isFocused} sel=${editor.selectionStart}..${editor.selectionEnd}" }
        Log.i("UiProbe", "editor after 2nd tap: $info")
        Thread.sleep(5000)
        shot("tap-editor-5s.png")
        scenario.onActivity { info = "sel=${editor.selectionStart}..${editor.selectionEnd}" }
        Log.i("UiProbe", "editor 5 s later: $info")
    }
}
