package io.github.leukoplast1980.mecrisppocket

import android.graphics.Bitmap
import android.util.Log
import android.view.inputmethod.EditorInfo
import android.widget.EditText
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.io.FileOutputStream

/** Types a few lines into the REPL, logs how the console lays them out, and takes a screenshot. */
@RunWith(AndroidJUnit4::class)
class ReplProbeTest {

    @Test
    fun replLines() {
        val scenario = ActivityScenario.launch(MainActivity::class.java)
        Thread.sleep(3000)
        for (line in listOf("3", "4", "+", ".", ": test 0 do i . loop ;", "t test", "5 test")) {
            scenario.onActivity { a ->
                val input = a.findViewById<EditText>(R.id.input)
                input.setText(line)
                input.onEditorAction(EditorInfo.IME_ACTION_SEND)
            }
            Thread.sleep(700)
        }
        Thread.sleep(1500)
        scenario.onActivity { a ->
            val out = a.findViewById<TextView>(R.id.output)
            val t = out.text.toString()
            for (l in t.takeLast(400).replace("\r", "<CR>").split("\n")) Log.i(TAG, "text|$l|")
            val lay = out.layout
            val pitch = if (lay != null && lay.lineCount > 1) lay.getLineTop(1) - lay.getLineTop(0) else -1
            Log.i(TAG, "console: class=${out.javaClass.name} lines=${out.lineCount} lineHeight=${out.lineHeight} " +
                "textSize=${out.textSize} pitch=$pitch fm=${out.paint.fontMetricsInt} " +
                "elegant=${out.isElegantTextHeight} fallback=${out.isFallbackLineSpacing} " +
                "mult=${out.lineSpacingMultiplier} extra=${out.lineSpacingExtra} pad=${out.includeFontPadding}")
            val ed = a.findViewById<TextView>(R.id.editor)
            Log.i(TAG, "editor: class=${ed.javaClass.name} lineHeight=${ed.lineHeight} textSize=${ed.textSize} " +
                "fm=${ed.paint.fontMetricsInt} elegant=${ed.isElegantTextHeight} fallback=${ed.isFallbackLineSpacing}")
        }
        val bmp = InstrumentationRegistry.getInstrumentation().uiAutomation.takeScreenshot()
        val dir = InstrumentationRegistry.getInstrumentation().targetContext.getExternalFilesDir(null)!!
        FileOutputStream(File(dir, "repl.png")).use { bmp.compress(Bitmap.CompressFormat.PNG, 100, it) }
        Log.i(TAG, "screenshot saved in $dir")
    }

    companion object {
        const val TAG = "UiProbe"
    }
}
