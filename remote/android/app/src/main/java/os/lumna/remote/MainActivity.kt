package os.lumna.remote

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.content.ActivityNotFoundException
import android.content.Intent
import android.graphics.Color
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.speech.RecognizerIntent
import android.text.InputType
import android.view.Gravity
import android.view.View
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.view.WindowInsets
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast

/**
 * LUMNA Connect: escolhe o PC na rede, pareia com o código que aparece na tela de Mídia e abre as telas do
 * app: Controle, Compartilhar (arquivos e área de transferência), Emma, PC (energia e agendamentos) e Ajustes.
 * A conexão e o estado moram em [Connect]; as telas só mostram.
 */
class MainActivity : Activity(), Host, Connect.Listener {
    private lateinit var discovery: Discovery
    private var found: List<Pc> = emptyList()
    private lateinit var ui: Ui

    private var screens: List<Screen> = emptyList()
    private var tabViews: List<LinearLayout> = emptyList()
    private var currentTab = 0
    private lateinit var body: FrameLayout
    private var pickerList: LinearLayout? = null
    private var voiceCallback: ((String) -> Unit)? = null

    override val activity: Activity get() = this

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Connect.init(this)
        ui = Ui(this)
        discovery = Discovery(this) { list ->
            found = list
            renderPickerList()
        }
        val last = Connect.store.last()
        if (last != null && Connect.store.token(last) != null) {
            if (Connect.pc == null) Connect.connect(last)
            showShell()
        } else {
            showPicker()
        }
        if (Connect.store.keepAlive) KeepAliveService.start(this)
        handleShare(intent)
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        handleShare(intent)
    }

    override fun onStart() {
        super.onStart()
        Connect.addListener(this)
    }

    override fun onResume() {
        super.onResume()
        discovery.start()
        if (Connect.pc != null && !Connect.connected) Connect.resume()
        if (Connect.store.autoClipboard && Connect.connected) window.decorView.post { Connect.sendClipboard(quiet = true) }
        screens.getOrNull(currentTab)?.onShow()
    }

    override fun onPause() {
        super.onPause()
        discovery.stop()
    }

    override fun onStop() {
        Connect.removeListener(this)
        super.onStop()
    }

    // ---- eventos da conexão ----

    override fun onEvent(event: Connect.Event) {
        when (event) {
            is Connect.Event.Toast -> Toast.makeText(this, event.text, Toast.LENGTH_SHORT).show()
            is Connect.Event.NeedCode -> {
                showPicker()
                askCode(event.pc)
            }
            else -> screens.forEach { it.onEvent(event) }
        }
    }

    // ---- as telas do app ----

    private fun showShell() {
        if (screens.isEmpty()) {
            screens = listOf(ControlScreen(this), ShareScreen(this), EmmaScreen(this), PcScreen(this), SettingsScreen(this))
        }
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        body = FrameLayout(this)
        root.addView(body, LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f))
        val bar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setBackgroundColor(ui.surface)
        }
        val tabs = listOf("🕹️" to "Controle", "📁" to "Compartilhar", "✨" to "Emma", "💻" to "PC", "⚙️" to "Ajustes")
        tabViews = tabs.mapIndexed { index, (icon, name) ->
            LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                gravity = Gravity.CENTER
                setPadding(0, ui.dp(8), 0, ui.dp(8))
                addView(TextView(this@MainActivity).apply { text = icon; textSize = 20f; gravity = Gravity.CENTER })
                addView(TextView(this@MainActivity).apply { text = name; textSize = 11f; gravity = Gravity.CENTER })
                setOnClickListener { selectTab(index) }
                bar.addView(this, LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
            }
        }
        root.addView(bar, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT))
        root.setOnApplyWindowInsetsListener { v, insets -> applyInsets(v, insets) }
        setContentView(root)
        selectTab(currentTab)
    }

    private fun selectTab(index: Int) {
        currentTab = index
        body.removeAllViews()
        // uma vista só vive num pai: tira da tela antiga antes de pôr na nova
        (screens[index].view.parent as? android.view.ViewGroup)?.removeView(screens[index].view)
        body.addView(screens[index].view, FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT))
        tabViews.forEachIndexed { i, tab ->
            val color = if (i == index) ui.violetSoft else ui.muted
            for (c in 0 until tab.childCount) (tab.getChildAt(c) as TextView).setTextColor(color)
        }
        screens[index].onShow()
    }

    @Suppress("DEPRECATION")
    private fun applyInsets(view: View, insets: WindowInsets): WindowInsets {
        if (Build.VERSION.SDK_INT >= 30) {
            val i = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.ime())
            view.setPadding(i.left, i.top, i.right, i.bottom)
        } else {
            view.setPadding(insets.systemWindowInsetLeft, insets.systemWindowInsetTop, insets.systemWindowInsetRight, insets.systemWindowInsetBottom)
        }
        return insets
    }

    // ---- escolher o PC e parear ----

    override fun showPicker() {
        Connect.disconnect()
        val content = ui.column()
        content.addView(ui.title("LUMNA Connect", "Escolha o PC. Ele precisa estar na mesma rede Wi-Fi."))
        val list = ui.column()
        pickerList = list
        content.addView(list)
        content.addView(ui.button("Digitar o endereço do PC") { askAddress() }, LinearLayout.LayoutParams(MATCH_PARENT, ui.dp(56)).apply { topMargin = ui.dp(18) })
        val page = ui.page(content)
        page.setOnApplyWindowInsetsListener { v, insets -> applyInsets(v, insets) }
        setContentView(page)
        renderPickerList()
    }

    private fun renderPickerList() {
        val list = pickerList ?: return
        list.removeAllViews()
        if (found.isEmpty()) list.addView(ui.label("Procurando PCs com LUMNA…", 15f, ui.muted))
        for (pc in found) {
            val paired = Connect.store.token(pc) != null
            list.addView(
                ui.button(pc.name + if (paired) "  ·  pareado" else "", primary = paired) { choose(pc) },
                LinearLayout.LayoutParams(MATCH_PARENT, ui.dp(60)).apply { bottomMargin = ui.dp(10) },
            )
        }
    }

    private fun askAddress() {
        val input = EditText(this).apply {
            hint = "192.168.0.10"
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_URI
        }
        AlertDialog.Builder(this)
            .setTitle("Endereço do PC")
            .setMessage("Aparece na tela de Mídia do PC, em \"Controle pelo celular\".")
            .setView(input)
            .setPositiveButton("Conectar") { _, _ ->
                val host = input.text.toString().trim()
                if (host.isNotEmpty()) choose(Pc(host, host, Protocol.PORT))
            }
            .setNegativeButton("Cancelar", null)
            .show()
    }

    private fun choose(pc: Pc) {
        if (Connect.store.token(pc) != null) {
            Connect.connect(pc)
            showShell()
        } else {
            askCode(pc)
        }
    }

    private fun askCode(pc: Pc) {
        val input = EditText(this).apply {
            hint = "6 dígitos"
            inputType = InputType.TYPE_CLASS_NUMBER
            textSize = 26f
            gravity = Gravity.CENTER
            letterSpacing = 0.3f
        }
        AlertDialog.Builder(this)
            .setTitle("Parear com ${pc.name}")
            .setMessage("No PC, vá ao modo Mídia e toque em \"Controle pelo celular\". Digite o código que aparecer.")
            .setView(input)
            .setPositiveButton("Parear") { _, _ ->
                val code = input.text.toString().filter(Char::isDigit)
                if (code.length == 6) {
                    Connect.pair(pc, code)
                    showShell()
                } else {
                    Toast.makeText(this, "O código tem 6 dígitos", Toast.LENGTH_SHORT).show()
                }
            }
            .setNegativeButton("Cancelar", null)
            .show()
    }

    // ---- o que as telas pedem (Host) ----

    override fun pickFiles() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "*/*"
            putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true)
        }
        startActivityForResult(intent, REQUEST_FILES)
    }

    /** O reconhecimento de voz do próprio celular (a tela do sistema, sem pedir permissão de microfone). */
    override fun listen(onText: (String) -> Unit) {
        voiceCallback = onText
        val intent = Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH).apply {
            putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM)
            putExtra(RecognizerIntent.EXTRA_LANGUAGE, "pt-BR")
            putExtra(RecognizerIntent.EXTRA_PROMPT, "Fale com a Emma")
        }
        try {
            startActivityForResult(intent, REQUEST_VOICE)
        } catch (e: ActivityNotFoundException) {
            Toast.makeText(this, "Este celular não tem reconhecimento de voz", Toast.LENGTH_SHORT).show()
        }
    }

    override fun requestNotificationPermission() {
        if (Build.VERSION.SDK_INT >= 33) requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), REQUEST_NOTIFICATIONS)
    }

    override fun openListenerSettings() {
        try {
            startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS))
        } catch (e: ActivityNotFoundException) {
            Toast.makeText(this, "Abra Configurações > Acesso às notificações", Toast.LENGTH_LONG).show()
        }
    }

    override fun setKeepAlive(on: Boolean) {
        if (on) {
            requestNotificationPermission() // o aviso fixo só aparece com a permissão (o serviço roda de qualquer jeito)
            KeepAliveService.start(this)
        } else {
            KeepAliveService.stop(this)
        }
    }

    @Deprecated("Activity.onActivityResult: o seletor de arquivos e o reconhecedor de voz devolvem por aqui.")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (resultCode != RESULT_OK || data == null) return
        when (requestCode) {
            REQUEST_VOICE -> {
                val said = data.getStringArrayListExtra(RecognizerIntent.EXTRA_RESULTS)?.firstOrNull()?.trim()
                if (!said.isNullOrEmpty()) voiceCallback?.invoke(said)
            }
            REQUEST_FILES -> {
                val clip = data.clipData
                if (clip != null) {
                    for (i in 0 until clip.itemCount) Transfers.upload(clip.getItemAt(i).uri)
                } else {
                    data.data?.let(Transfers::upload)
                }
            }
        }
    }

    /** Compartilhar > LUMNA Connect, de qualquer app: arquivos vão para o PC e texto vai para a área de transferência dele. */
    @Suppress("DEPRECATION")
    private fun handleShare(intent: Intent?) {
        intent ?: return
        val action = intent.action
        if (action != Intent.ACTION_SEND && action != Intent.ACTION_SEND_MULTIPLE) return
        intent.action = null // não repetir ao girar a tela
        val uris = if (action == Intent.ACTION_SEND) listOfNotNull(intent.getParcelableExtra<Uri>(Intent.EXTRA_STREAM))
        else intent.getParcelableArrayListExtra<Uri>(Intent.EXTRA_STREAM).orEmpty()
        val text = intent.getStringExtra(Intent.EXTRA_TEXT)
        window.decorView.post {
            if (Connect.pc == null) {
                Toast.makeText(this, "Conecte a um PC primeiro", Toast.LENGTH_SHORT).show()
            } else if (uris.isNotEmpty()) {
                uris.forEach(Transfers::upload)
                if (screens.isNotEmpty()) selectTab(1)
            } else if (!text.isNullOrEmpty()) {
                Connect.sendSharedText(text)
            }
        }
    }

    @Deprecated("Activity.onBackPressed: o voltar do celular leva ao Controle antes de sair do app.")
    override fun onBackPressed() {
        if (screens.isNotEmpty() && currentTab != 0) selectTab(0) else super.onBackPressed()
    }

    private companion object {
        const val REQUEST_VOICE = 4701
        const val REQUEST_FILES = 4702
        const val REQUEST_NOTIFICATIONS = 4703
    }
}
