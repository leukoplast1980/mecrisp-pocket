/*
 * Mecrisp Pocket - main screen: REPL and Registers, as tabs on phones and
 * side by side on wide screens.
 * Copyright (C) 2026 The Mecrisp Pocket authors. GPL-3.0-or-later.
 */
package io.github.leukoplast1980.mecrisppocket

import android.content.res.Configuration
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.KeyEvent
import android.view.View
import android.view.inputmethod.InputMethodManager
import android.widget.Button
import android.widget.ImageButton
import android.widget.PopupMenu
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import com.google.android.material.tabs.TabLayout
import io.github.leukoplast1980.mecrisppocket.ui.ConsoleController
import io.github.leukoplast1980.mecrisppocket.ui.RegistersController
import io.github.leukoplast1980.mecrisppocket.vm.VmHost

class MainActivity : AppCompatActivity() {

    private lateinit var vm: VmHost
    private lateinit var console: ConsoleController
    private lateinit var registers: RegistersController

    private lateinit var tabs: TabLayout
    private lateinit var replPane: View
    private lateinit var registersPane: View
    private lateinit var divider: View
    private lateinit var title: TextView

    private val handler = Handler(Looper.getMainLooper())
    private var started = false
    private var registersTab = false

    private val poll = object : Runnable {
        override fun run() {
            console.poll()
            updateTitle()
            handler.postDelayed(this, POLL_MS)
        }
    }

    private val registerRefresh = object : Runnable {
        override fun run() {
            if (registersVisible()) registers.refresh()
            handler.postDelayed(this, REGISTERS_MS)
        }
    }

    private val autosave = object : Runnable {
        override fun run() {
            Thread { vm.saveFlash() }.start()
            handler.postDelayed(this, AUTOSAVE_MS)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        WindowCompat.setDecorFitsSystemWindows(window, false)
        setContentView(R.layout.activity_main)
        // The input line is not a login form: keep password managers away.
        window.decorView.importantForAutofill = View.IMPORTANT_FOR_AUTOFILL_NO_EXCLUDE_DESCENDANTS

        vm = VmHost.get(this)
        console = ConsoleController(this, vm)
        registers = RegistersController(this, vm)

        tabs = findViewById(R.id.tabs)
        replPane = findViewById(R.id.replPane)
        registersPane = findViewById(R.id.registersPane)
        divider = findViewById(R.id.paneDivider)
        title = findViewById(R.id.title)

        val root = findViewById<View>(R.id.root)
        ViewCompat.setOnApplyWindowInsetsListener(root) { v, insets ->
            val bars = insets.getInsets(WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.ime())
            v.setPadding(bars.left, bars.top, bars.right, bars.bottom)
            WindowInsetsCompat.CONSUMED
        }

        findViewById<Button>(R.id.stop).setOnClickListener {
            vm.stop()
            console.markDictionaryStale()
        }
        findViewById<Button>(R.id.reset).apply {
            setOnClickListener {
                vm.reset(safeBoot = false)
                console.markDictionaryStale()
            }
            setOnLongClickListener {
                vm.reset(safeBoot = true)
                console.markDictionaryStale()
                Toast.makeText(this@MainActivity, R.string.safe_boot, Toast.LENGTH_SHORT).show()
                true
            }
        }
        findViewById<ImageButton>(R.id.menu).setOnClickListener { showMenu(it) }

        tabs.addTab(tabs.newTab().setText(R.string.tab_repl))
        tabs.addTab(tabs.newTab().setText(R.string.tab_registers))
        tabs.addOnTabSelectedListener(object : TabLayout.OnTabSelectedListener {
            override fun onTabSelected(tab: TabLayout.Tab) {
                registersTab = tab.position == 1
                applyLayout()
            }
            override fun onTabUnselected(tab: TabLayout.Tab) {}
            override fun onTabReselected(tab: TabLayout.Tab) {}
        })

        registers.onPausedChanged = { console.markDictionaryStale() }

        if (savedInstanceState != null) {
            savedInstanceState.getCharSequence(KEY_OUTPUT)?.let { console.appendText(it.toString()) }
            if (savedInstanceState.getBoolean(KEY_RAW)) console.setRawMode(true)
            if (savedInstanceState.getBoolean(KEY_REGISTERS_TAB)) tabs.getTabAt(1)?.select()
        }
        if (vm.coreUpdateAvailable) console.appendText(getString(R.string.core_update_notice))

        applyLayout()
    }

    private fun isWide(): Boolean = resources.configuration.screenWidthDp >= WIDE_DP

    private fun registersVisible(): Boolean = isWide() || registersTab

    private fun applyLayout() {
        val wide = isWide()
        tabs.visibility = if (wide) View.GONE else View.VISIBLE
        divider.visibility = if (wide) View.VISIBLE else View.GONE
        replPane.visibility = if (wide || !registersTab) View.VISIBLE else View.GONE
        registersPane.visibility = if (registersVisible()) View.VISIBLE else View.GONE
        if (!wide && registersTab) {
            getSystemService(InputMethodManager::class.java)
                ?.hideSoftInputFromWindow(registersPane.windowToken, 0)
        }
        if (registersVisible() && started) registers.refresh()
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        applyLayout()
    }

    private fun showMenu(anchor: View) {
        val menu = PopupMenu(this, anchor)
        menu.menuInflater.inflate(R.menu.console, menu.menu)
        menu.menu.findItem(R.id.raw_mode).isChecked = console.rawMode
        menu.setOnMenuItemClickListener { item ->
            when (item.itemId) {
                R.id.raw_mode -> console.setRawMode(!console.rawMode)
                R.id.copy_all -> console.copyAll()
                R.id.clear_screen -> console.clear()
            }
            true
        }
        menu.show()
    }

    private fun updateTitle() {
        val name = getString(R.string.app_name)
        title.text = if (vm.isUserPaused) getString(R.string.paused_suffix, name) else name
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (event.action == KeyEvent.ACTION_DOWN) {
            if (event.isCtrlPressed) {
                when (event.keyCode) {
                    KeyEvent.KEYCODE_C -> { vm.stop(); console.markDictionaryStale(); return true }
                    KeyEvent.KEYCODE_L -> { console.clear(); return true }
                }
            }
            if (event.keyCode == KeyEvent.KEYCODE_TAB && replPane.visibility == View.VISIBLE) {
                console.complete()
                return true
            }
        }
        return super.dispatchKeyEvent(event)
    }

    override fun onStart() {
        super.onStart()
        started = true
        vm.setForeground(true)
        handler.post(poll)
        handler.post(registerRefresh)
        handler.postDelayed(autosave, AUTOSAVE_MS)
    }

    override fun onStop() {
        started = false
        handler.removeCallbacks(poll)
        handler.removeCallbacks(registerRefresh)
        handler.removeCallbacks(autosave)
        vm.setForeground(false)
        console.history.save()
        Thread { vm.saveFlash() }.start()
        super.onStop()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putCharSequence(KEY_OUTPUT, console.savedText())
        outState.putBoolean(KEY_RAW, console.rawMode)
        outState.putBoolean(KEY_REGISTERS_TAB, registersTab)
    }

    companion object {
        private const val POLL_MS = 30L
        private const val REGISTERS_MS = 50L
        private const val AUTOSAVE_MS = 5000L
        private const val WIDE_DP = 600
        private const val KEY_OUTPUT = "output"
        private const val KEY_RAW = "raw"
        private const val KEY_REGISTERS_TAB = "registersTab"
    }
}
