package io.github.leukoplast1980.mecrisppocket

import android.graphics.Bitmap
import android.util.Log
import android.view.inputmethod.EditorInfo
import android.widget.EditText
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.google.android.material.tabs.TabLayout
import io.github.leukoplast1980.mecrisppocket.gpio.GpioView
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.io.FileOutputStream

/**
 * Drives the phone devices and the GPIO tab from Forth: brightness reaches
 * the window, a tapped input pin reaches Forth, and the GPIO view gets a
 * screenshot (gpio.png, pulled by the UI probe workflow).
 */
@RunWith(AndroidJUnit4::class)
class DevicesProbeTest {

    private fun send(scenario: ActivityScenario<MainActivity>, line: String) {
        scenario.onActivity { a ->
            val input = a.findViewById<EditText>(R.id.input)
            input.setText(line)
            input.onEditorAction(EditorInfo.IME_ACTION_SEND)
        }
        Thread.sleep(600)
    }

    private fun console(scenario: ActivityScenario<MainActivity>): String {
        var t = ""
        scenario.onActivity { a -> t = a.findViewById<TextView>(R.id.output).text.toString() }
        return t
    }

    private fun selectTab(scenario: ActivityScenario<MainActivity>, name: String) {
        scenario.onActivity { a ->
            val tabs = a.findViewById<TabLayout>(R.id.tabs)
            for (i in 0 until tabs.tabCount) if (tabs.getTabAt(i)?.text == name) tabs.getTabAt(i)?.select()
        }
        Thread.sleep(800)
    }

    private fun screenshot(name: String) {
        val bmp = InstrumentationRegistry.getInstrumentation().uiAutomation.takeScreenshot()
        val dir = InstrumentationRegistry.getInstrumentation().targetContext.getExternalFilesDir(null)!!
        FileOutputStream(File(dir, name)).use { bmp.compress(Bitmap.CompressFormat.PNG, 100, it) }
    }

    @Test
    fun devicesAndGpio() {
        val scenario = ActivityScenario.launch(MainActivity::class.java)
        Thread.sleep(3000)

        // Brightness: the board's value reaches the window.
        send(scenario, "100 brightness")
        var brightness = 0f
        scenario.onActivity { a -> brightness = a.window.attributes.screenBrightness }
        assertEquals(100 / 255f, brightness, 0.01f)
        send(scenario, "-1 brightness")

        // A tone and a vibration must not upset anything.
        send(scenario, "2 waveform 880 300 tone 100 vibrate")

        // Pins: an output that blinks, and an input driven from the GPIO view.
        send(scenario, ": blink 20 0 do 0 pin-toggle 1000 0 do loop loop ;")
        send(scenario, "0 pin-out 1 pin-out 1 pin-high blink 3 pin-out")
        selectTab(scenario, "GPIO")
        scenario.onActivity { a -> a.findViewById<GpioView>(R.id.gpioView).onPinTapped?.invoke(5) }
        Thread.sleep(500)
        screenshot("gpio.png")
        selectTab(scenario, "REPL")
        send(scenario, "5 pin@ . 6 pin@ . pins@ .")

        val text = console(scenario)
        for (l in text.takeLast(600).split("\n")) Log.i(TAG, "text|$l|")
        assertTrue("tapped input pin not seen by Forth:\n$text", text.contains("5 pin@ . 6 pin@ . pins@ . -1 0 34  ok."))
        assertTrue("error in the console:\n$text", !text.contains("not found") && !text.contains("Exception"))
    }

    companion object {
        const val TAG = "UiProbe"
    }
}
