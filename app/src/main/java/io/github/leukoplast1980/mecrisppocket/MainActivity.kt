/*
 * Mecrisp Pocket - main screen. Phones: REPL, Editor and Registers as tabs.
 * Wide screens: the editor on the left, REPL and Registers as tabs on the right.
 * Copyright (C) 2026 The Mecrisp Pocket authors. GPL-3.0-or-later.
 */
package io.github.leukoplast1980.mecrisppocket

import android.content.Intent
import android.content.res.Configuration
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.KeyEvent
import android.view.View
import android.view.WindowManager
import android.view.inputmethod.InputMethodManager
import android.widget.Button
import android.widget.HorizontalScrollView
import android.widget.ImageButton
import android.widget.LinearLayout
import android.widget.PopupMenu
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import com.google.android.material.tabs.TabLayout
import io.github.leukoplast1980.mecrisppocket.files.ProjectFolder
import io.github.leukoplast1980.mecrisppocket.files.SessionStore
import io.github.leukoplast1980.mecrisppocket.settings.Settings
import io.github.leukoplast1980.mecrisppocket.settings.SettingsActivity
import io.github.leukoplast1980.mecrisppocket.ui.Completion
import io.github.leukoplast1980.mecrisppocket.ui.ConsoleController
import io.github.leukoplast1980.mecrisppocket.ui.EditorController
import io.github.leukoplast1980.mecrisppocket.ui.ExtraKeys
import io.github.leukoplast1980.mecrisppocket.ui.InputTarget
import io.github.leukoplast1980.mecrisppocket.devices.PhoneDevices
import io.github.leukoplast1980.mecrisppocket.gpio.GpioController
import io.github.leukoplast1980.mecrisppocket.ui.RegistersController
import io.github.leukoplast1980.mecrisppocket.ui.SuggestionStrip
import io.github.leukoplast1980.mecrisppocket.vm.VmHost

class MainActivity : AppCompatActivity() {

    private enum class Screen { REPL, EDITOR, REGISTERS, GPIO }

    private lateinit var vm: VmHost
    private lateinit var console: ConsoleController
    private lateinit var editor: EditorController
    private lateinit var registers: RegistersController
    private lateinit var gpio: GpioController
    private lateinit var devices: PhoneDevices
    private lateinit var strip: SuggestionStrip
    private lateinit var folder: ProjectFolder
    private lateinit var sessionStore: SessionStore
    private lateinit var settings: Settings

    private lateinit var tabs: TabLayout
    private lateinit var editorPane: View
    private lateinit var rightPane: View
    private lateinit var replPane: View
    private lateinit var registersPane: View
    private lateinit var gpioPane: View
    private lateinit var divider: View
    private lateinit var extraKeysScroll: View
    private lateinit var title: TextView

    private val handler = Handler(Looper.getMainLooper())
    private var started = false
    private var screen = Screen.REPL
    private var tabsWide: Boolean? = null
    private var rebuildingTabs = false

    /** The input that last had focus, for the strip and extra keys on wide screens. */
    private var lastInput: InputTarget? = null

    private val pickFolder = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) {
            folder.setTree(uri)
            editor.showFiles()
        }
    }

    private val poll = object : Runnable {
        override fun run() {
            console.poll()
            devices.poll()
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

    private val gpioRefresh = object : Runnable {
        override fun run() {
            if (screen == Screen.GPIO) gpio.refresh()
            handler.postDelayed(this, GPIO_MS)
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
        folder = ProjectFolder(this)
        sessionStore = SessionStore(this)
        settings = Settings(this)

        strip = SuggestionStrip(this, findViewById<HorizontalScrollView>(R.id.suggestionScroll), findViewById(R.id.suggestions))
        console = ConsoleController(
            this, vm,
            onInputChanged = { updateStrip() },
            onDictionaryChanged = { editor.scheduleHighlight(); updateStrip() },
        )
        editor = EditorController(
            this, vm, folder,
            onInputChanged = { updateStrip() },
            onSent = { console.markDictionaryStale() },
            pickFolder = { pickFolder.launch(folder.treeUri) },
        )
        registers = RegistersController(this, vm)
        registers.onPausedChanged = { console.markDictionaryStale() }
        gpio = GpioController(this, vm)
        devices = PhoneDevices(this, vm)
        ExtraKeys(findViewById<LinearLayout>(R.id.extraKeys)) { activeInput() }

        tabs = findViewById(R.id.tabs)
        editorPane = findViewById(R.id.editorPane)
        rightPane = findViewById(R.id.rightPane)
        replPane = findViewById(R.id.replPane)
        registersPane = findViewById(R.id.registersPane)
        gpioPane = findViewById(R.id.gpioPane)
        divider = findViewById(R.id.paneDivider)
        extraKeysScroll = findViewById(R.id.extraKeysScroll)
        title = findViewById(R.id.title)

        val root = findViewById<View>(R.id.root)
        ViewCompat.setOnApplyWindowInsetsListener(root) { v, insets ->
            val bars = insets.getInsets(WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.ime())
            v.setPadding(bars.left, bars.top, bars.right, bars.bottom)
            WindowInsetsCompat.CONSUMED
        }

        findViewById<Button>(R.id.stop).setOnClickListener { stop() }
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

        tabs.addOnTabSelectedListener(object : TabLayout.OnTabSelectedListener {
            override fun onTabSelected(tab: TabLayout.Tab) {
                if (rebuildingTabs) return
                screen = tab.tag as Screen
                applyLayout()
            }
            override fun onTabUnselected(tab: TabLayout.Tab) {}
            override fun onTabReselected(tab: TabLayout.Tab) {}
        })

        // Track which input was used last (both are visible on wide screens).
        val trackFocus = View.OnFocusChangeListener { v, hasFocus ->
            if (hasFocus) {
                lastInput = if (v === editor.editor) editor else console
                updateStrip()
            }
        }
        editor.editor.onFocusChangeListener = trackFocus
        console.input.onFocusChangeListener = trackFocus
        console.rawInput.onFocusChangeListener = trackFocus

        // Restore the last session.
        val session = sessionStore.load()
        if (savedInstanceState == null) {
            val text = sessionStore.loadConsole()
            if (text.isNotEmpty()) console.appendText(text)
        } else {
            savedInstanceState.getCharSequence(KEY_OUTPUT)?.let { console.appendText(it.toString()) }
        }
        if (session.rawMode) console.setRawMode(true)
        screen = runCatching { Screen.valueOf(session.screen) }.getOrDefault(Screen.REPL)
        editor.restore(session.files, session.activeFile)
        if (vm.coreUpdateAvailable) console.appendText(getString(R.string.core_update_notice))
        console.scrollToEnd()

        applyLayout()
    }

    private fun isWide(): Boolean = resources.configuration.screenWidthDp >= WIDE_DP

    private fun registersVisible(): Boolean = screen == Screen.REGISTERS

    /** Tabs: REPL, Editor, Registers, GPIO on phones; the same without Editor on wide screens. */
    private fun rebuildTabs(wide: Boolean) {
        if (tabsWide == wide) return
        tabsWide = wide
        rebuildingTabs = true
        tabs.removeAllTabs()
        val order = if (wide) listOf(Screen.REPL, Screen.REGISTERS, Screen.GPIO)
        else listOf(Screen.REPL, Screen.EDITOR, Screen.REGISTERS, Screen.GPIO)
        for (s in order) {
            val label = when (s) {
                Screen.REPL -> R.string.tab_repl
                Screen.EDITOR -> R.string.tab_editor
                Screen.REGISTERS -> R.string.tab_registers
                Screen.GPIO -> R.string.tab_gpio
            }
            tabs.addTab(tabs.newTab().setText(label).setTag(s), false)
        }
        rebuildingTabs = false
    }

    private fun applyLayout() {
        val wide = isWide()
        rebuildTabs(wide)
        if (wide && screen == Screen.EDITOR) screen = Screen.REPL
        rebuildingTabs = true
        for (i in 0 until tabs.tabCount) {
            val t = tabs.getTabAt(i) ?: continue
            if (t.tag == screen && !t.isSelected) t.select()
        }
        rebuildingTabs = false

        editorPane.visibility = if (wide || screen == Screen.EDITOR) View.VISIBLE else View.GONE
        divider.visibility = if (wide) View.VISIBLE else View.GONE
        rightPane.visibility = if (wide || screen != Screen.EDITOR) View.VISIBLE else View.GONE
        val tool = screen == Screen.REGISTERS || screen == Screen.GPIO
        replPane.visibility = if (tool) View.GONE else View.VISIBLE
        registersPane.visibility = if (screen == Screen.REGISTERS) View.VISIBLE else View.GONE
        gpioPane.visibility = if (screen == Screen.GPIO) View.VISIBLE else View.GONE

        if (!wide) {
            when (screen) {
                Screen.REPL -> console.focusInput()
                Screen.EDITOR -> editor.editor.requestFocus()
                Screen.REGISTERS, Screen.GPIO -> getSystemService(InputMethodManager::class.java)
                    ?.hideSoftInputFromWindow(rightPane.windowToken, 0)
            }
        }
        if (registersVisible() && started) registers.refresh()
        if (screen == Screen.GPIO && started) gpio.refresh()
        updateStrip()
    }

    /** Where the suggestion strip and the extra keys type into. */
    private fun activeInput(): InputTarget? {
        if (!isWide()) {
            return when (screen) {
                Screen.REPL -> console
                Screen.EDITOR -> editor
                Screen.REGISTERS, Screen.GPIO -> null
            }
        }
        if (editor.editor.hasFocus()) return editor
        if (screen == Screen.REGISTERS || screen == Screen.GPIO) return editor
        return lastInput ?: console
    }

    private fun updateStrip() {
        val target = activeInput()
        strip.update(target)
        extraKeysScroll.visibility = if (target == null) View.GONE else View.VISIBLE
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        applyLayout()
    }

    private fun stop() {
        vm.stop()
        console.markDictionaryStale()
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
                R.id.project_folder -> pickFolder.launch(folder.treeUri)
                R.id.settings -> startActivity(Intent(this, SettingsActivity::class.java))
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
            val inEditor = editor.editor.hasFocus()
            if (inEditor && editor.onKeyShortcut(event)) return true
            if (event.isCtrlPressed) {
                when (event.keyCode) {
                    // In the editor Ctrl+C copies; elsewhere it stops Forth.
                    KeyEvent.KEYCODE_C -> if (!inEditor) { stop(); return true }
                    KeyEvent.KEYCODE_L -> { console.clear(); return true }
                    KeyEvent.KEYCODE_SPACE -> { activeInput()?.let { Completion.tab(it) }; return true }
                }
            }
            if (event.keyCode == KeyEvent.KEYCODE_TAB && !event.isCtrlPressed) {
                val target = activeInput()
                if (target != null) {
                    if (inEditor && target.currentToken().isEmpty()) target.insertText("  ")
                    else Completion.tab(target)
                    return true
                }
            }
        }
        return super.dispatchKeyEvent(event)
    }

    override fun onStart() {
        super.onStart()
        applySettings()
        started = true
        vm.setForeground(true)
        handler.post(poll)
        handler.post(registerRefresh)
        handler.post(gpioRefresh)
        handler.postDelayed(autosave, AUTOSAVE_MS)
    }

    /** Settings may have changed on the Settings screen. */
    private fun applySettings() {
        val sp = settings.textSizeSp.toFloat()
        editor.editor.setTextSizeSp(sp)
        editor.editor.setLineWrap(settings.lineWrap)
        console.setTextSizeSp(sp)
        if (settings.keepScreenOn) window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        else window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    }

    override fun onStop() {
        started = false
        handler.removeCallbacks(poll)
        handler.removeCallbacks(registerRefresh)
        handler.removeCallbacks(gpioRefresh)
        devices.quiet()
        handler.removeCallbacks(autosave)
        vm.setForeground(false)
        console.history.save()
        editor.saveAll()
        sessionStore.save(
            SessionStore.Session(
                files = editor.sessionFiles(),
                activeFile = editor.activeIndex().coerceAtLeast(0),
                screen = screen.name,
                rawMode = console.rawMode,
            )
        )
        sessionStore.saveConsole(console.savedText())
        Thread { vm.saveFlash() }.start()
        super.onStop()
    }

    override fun onDestroy() {
        if (::devices.isInitialized) devices.release()
        super.onDestroy()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putCharSequence(KEY_OUTPUT, console.savedText())
    }

    companion object {
        private const val POLL_MS = 30L
        private const val REGISTERS_MS = 50L
        private const val GPIO_MS = 33L
        private const val AUTOSAVE_MS = 5000L
        private const val WIDE_DP = 600
        private const val KEY_OUTPUT = "output"
    }
}
