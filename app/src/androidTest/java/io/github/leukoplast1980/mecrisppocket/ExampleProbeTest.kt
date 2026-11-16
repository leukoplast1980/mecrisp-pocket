package io.github.leukoplast1980.mecrisppocket

import android.view.ViewGroup
import android.view.inputmethod.EditorInfo
import android.widget.EditText
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import io.github.leukoplast1980.mecrisppocket.forth.LineResult
import io.github.leukoplast1980.mecrisppocket.vm.VmHost
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** The bundled light show: opened on the first start, loads cleanly, runs and stops. */
@RunWith(AndroidJUnit4::class)
class ExampleProbeTest {

    private fun send(scenario: ActivityScenario<MainActivity>, line: String) {
        scenario.onActivity { a ->
            val input = a.findViewById<EditText>(R.id.input)
            input.setText(line)
            input.onEditorAction(EditorInfo.IME_ACTION_SEND)
        }
    }

    @Test
    fun lightshow() {
        val scenario = ActivityScenario.launch(MainActivity::class.java)
        Thread.sleep(3000)

        // The first start of the app (whichever test was first) opened the example.
        var tabs = listOf<String>()
        scenario.onActivity { a ->
            val row = a.findViewById<ViewGroup>(R.id.fileTabs)
            tabs = (0 until row.childCount).map { row.getChildAt(it).findViewById<TextView>(R.id.tabName).text.toString() }
        }
        assertTrue("open tabs: $tabs", tabs.contains("lightshow.fs"))

        // Every line of the example is accepted, as Send > Whole file would send it.
        val ctx = InstrumentationRegistry.getInstrumentation().targetContext
        val source = ctx.assets.open("examples/lightshow.fs").bufferedReader().readText()
        val vm = VmHost.get(ctx)
        for ((n, line) in source.lines().withIndex()) {
            assertEquals("lightshow.fs:${n + 1}: $line", LineResult.OK, vm.sendLineChecked(line, 10_000))
        }

        send(scenario, "lightshow")
        Thread.sleep(2500)
        send(scenario, "")
        Thread.sleep(1000)
        var console = ""
        scenario.onActivity { a -> console = a.findViewById<TextView>(R.id.output).text.toString() }
        val tail = console.takeLast(1500)
        assertTrue("no sweep in:\n$tail", tail.contains("Sweep 1 mode 0"))
        assertTrue("did not stop:\n$tail", tail.contains("Show over. ok."))
    }
}
