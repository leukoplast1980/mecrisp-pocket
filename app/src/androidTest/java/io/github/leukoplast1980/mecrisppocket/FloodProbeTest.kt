package io.github.leukoplast1980.mecrisppocket

import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import android.view.inputmethod.EditorInfo
import android.widget.EditText
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import io.github.leukoplast1980.mecrisppocket.vm.VmHost
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/**
 * A program that prints without end must not freeze the app: the console
 * takes a bounded amount of output per refresh, and Stop still works.
 */
@RunWith(AndroidJUnit4::class)
class FloodProbeTest {

    private fun send(scenario: ActivityScenario<MainActivity>, line: String) = scenario.onActivity { a ->
        val input = a.findViewById<EditText>(R.id.input)
        input.setText(line)
        input.onEditorAction(EditorInfo.IME_ACTION_SEND)
    }

    @Test
    fun uiStaysResponsive() {
        val scenario = ActivityScenario.launch(MainActivity::class.java)
        Thread.sleep(2500)
        send(scenario, ": flood begin 12345 . again ; flood")
        Thread.sleep(1500)

        val delays = ArrayList<Long>()
        for (i in 0 until 5) {
            val latch = CountDownLatch(1)
            val t0 = SystemClock.uptimeMillis()
            Handler(Looper.getMainLooper()).post { latch.countDown() }
            val ran = latch.await(5, TimeUnit.SECONDS)
            delays.add(if (ran) SystemClock.uptimeMillis() - t0 else -1)
            Thread.sleep(200)
        }
        VmHost.get(InstrumentationRegistry.getInstrumentation().targetContext).stop()
        Thread.sleep(1000)
        send(scenario, "6 7 * .")
        Thread.sleep(1500)

        var tail = ""
        scenario.onActivity { a ->
            val t = a.findViewById<TextView>(R.id.output).text
            tail = t.subSequence(maxOf(0, t.length - 200), t.length).toString()
        }
        Log.i("UiProbe", "flood: main thread delays $delays, console ends with: ${tail.takeLast(60)}")
        assertTrue("main thread blocked: $delays", delays.all { it in 0..1000 })
        assertTrue("no answer after Stop: $tail", Regex("""42 +ok\.""").containsMatchIn(tail))
        scenario.close()
    }
}
