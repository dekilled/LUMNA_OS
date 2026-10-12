package os.lumna.remote

import android.app.Activity
import android.app.AlertDialog
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Build
import android.os.Bundle
import android.text.InputType
import android.view.Gravity
import android.view.HapticFeedbackConstants
import android.view.View
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.view.inputmethod.EditorInfo
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast

/**
 * LUMNA Controle: escolhe a TV na rede, pareia com o código que aparece na tela de Mídia e vira
 * controle remoto (direcional, voltar, início, volume, mídia, modos e teclado).
 */
class MainActivity : Activity(), TvLink.Listener {
    private lateinit var store: Store
    private lateinit var discovery: Discovery
    private var tvs: List<Tv> = emptyList()

    private var tv: Tv? = null
    private var link: TvLink? = null
    /** Código digitado esperando a TV responder (para saber se a falha foi de pareamento). */
    private var pairing = false

    private lateinit var status: TextView
    private lateinit var tvList: LinearLayout

    private val violetColor = Color.parseColor("#8B5CF6")
    private val surfaceColor = Color.parseColor("#1A1433")
    private val textColor = Color.parseColor("#ECE8FF")
    private val mutedColor = Color.parseColor("#A59CC9")

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        store = Store(this)
        discovery = Discovery(this) { found ->
            tvs = found
            if (tv == null) renderTvList()
        }
        val last = store.last()
        if (last != null && store.token(last) != null) connect(last) else showPicker()
    }

    override fun onResume() {
        super.onResume()
        discovery.start()
    }

    override fun onPause() {
        super.onPause()
        discovery.stop()
    }

    override fun onDestroy() {
        link?.close()
        super.onDestroy()
    }

    // ---- telas ----

    private fun dp(value: Int) = (value * resources.displayMetrics.density).toInt()

    private fun label(value: String, size: Float = 15f, color: Int = textColor, bold: Boolean = false) = TextView(this).apply {
        this.text = value
        textSize = size
        setTextColor(color)
        if (bold) typeface = Typeface.DEFAULT_BOLD
    }

    private fun button(value: String, primary: Boolean = false, onClick: () -> Unit) = Button(this).apply {
        text = value
        isAllCaps = false
        textSize = 15f
        setTextColor(Color.WHITE)
        background = GradientDrawable().apply {
            cornerRadius = dp(16).toFloat()
            setColor(if (primary) violetColor else surfaceColor)
        }
        stateListAnimator = null
        setOnClickListener {
            it.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
            onClick()
        }
    }

    private fun row(vararg views: View) = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        for (v in views) addView(v, LinearLayout.LayoutParams(0, dp(56), 1f).apply { setMargins(dp(5), dp(5), dp(5), dp(5)) })
    }

    private fun page(content: LinearLayout): View = ScrollView(this).apply {
        isFillViewport = true
        addView(content.apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(20), dp(28), dp(20), dp(24))
        })
    }

    /** Lista as TVs achadas na rede e a opção de digitar o endereço. */
    private fun showPicker() {
        tv = null
        link?.close()
        link = null
        val content = LinearLayout(this)
        content.addView(label("LUMNA Controle", 28f, bold = true))
        content.addView(label("Escolha a TV. Ela precisa estar na mesma rede Wi-Fi.", 14f, mutedColor).apply { setPadding(0, dp(6), 0, dp(18)) })
        tvList = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        content.addView(tvList)
        content.addView(button("Digitar o endereço da TV") { askAddress() }, LinearLayout.LayoutParams(MATCH_PARENT, dp(56)).apply { topMargin = dp(18) })
        setContentView(page(content))
        renderTvList()
    }

    private fun renderTvList() {
        if (!::tvList.isInitialized || tv != null) return
        tvList.removeAllViews()
        if (tvs.isEmpty()) {
            tvList.addView(label("Procurando TVs com LUMNA…", 15f, mutedColor))
        }
        for (found in tvs) {
            val paired = store.token(found) != null
            tvList.addView(
                button(found.name + if (paired) "  ·  pareada" else "", primary = paired) { choose(found) },
                LinearLayout.LayoutParams(MATCH_PARENT, dp(60)).apply { bottomMargin = dp(10) },
            )
        }
    }

    private fun askAddress() {
        val input = EditText(this).apply {
            hint = "192.168.0.10"
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_URI
        }
        AlertDialog.Builder(this)
            .setTitle("Endereço da TV")
            .setMessage("Aparece na tela de Mídia, em \"Controle pelo celular\".")
            .setView(input)
            .setPositiveButton("Conectar") { _, _ ->
                val host = input.text.toString().trim()
                if (host.isNotEmpty()) choose(Tv(host, host, Protocol.PORT))
            }
            .setNegativeButton("Cancelar", null)
            .show()
    }

    private fun choose(target: Tv) {
        if (store.token(target) != null) connect(target) else askCode(target)
    }

    private fun askCode(target: Tv) {
        val input = EditText(this).apply {
            hint = "6 dígitos"
            inputType = InputType.TYPE_CLASS_NUMBER
            textSize = 26f
            gravity = Gravity.CENTER
            letterSpacing = 0.3f
        }
        AlertDialog.Builder(this)
            .setTitle("Parear com ${target.name}")
            .setMessage("Na TV, vá ao modo Mídia e toque em \"Controle pelo celular\". Digite o código que aparecer.")
            .setView(input)
            .setPositiveButton("Parear") { _, _ ->
                val code = input.text.toString().filter(Char::isDigit)
                if (code.length == 6) pair(target, code) else toast("O código tem 6 dígitos")
            }
            .setNegativeButton("Cancelar", null)
            .show()
    }

    // ---- conexão ----

    private fun deviceName() = "${Build.MANUFACTURER.replaceFirstChar(Char::uppercase)} ${Build.MODEL}".take(40)

    private fun pair(target: Tv, code: String) {
        open(target, Protocol.pair(code, deviceName()))
        pairing = true
    }

    private fun connect(target: Tv) {
        val token = store.token(target) ?: return askCode(target)
        open(target, Protocol.hello(token))
    }

    private fun open(target: Tv, first: String) {
        link?.close()
        tv = target
        pairing = false
        showRemote(target)
        status.text = "Conectando…"
        link = TvLink(target.host, target.port, this).also { it.open(first) }
    }

    override fun onConnected() {
        status.text = "Conectado"
    }

    override fun onReply(reply: Protocol.Reply) {
        val current = tv ?: return
        when (reply) {
            is Protocol.Reply.Paired -> {
                store.saveToken(current, reply.token)
                pairing = false
                status.text = "Pareado com ${reply.tvName}"
            }
            is Protocol.Reply.Ready -> {
                store.remember(current)
                status.text = reply.tvName
            }
            is Protocol.Reply.Volume -> status.text = if (reply.muted) "Som desligado" else "Volume ${reply.value ?: "-"}%"
            is Protocol.Reply.Error -> {
                if (reply.unpaired || pairing) {
                    // Token velho (a TV esqueceu este celular) ou código errado: pede o código de novo.
                    store.forget(current)
                    toast(reply.message)
                    showPicker()
                    askCode(current)
                } else {
                    toast(reply.message)
                }
            }
            Protocol.Reply.Ok -> {}
        }
    }

    override fun onClosed(reason: String) {
        if (tv != null && ::status.isInitialized) status.text = "Desconectado: $reason · toque para reconectar"
    }

    private fun send(message: String) {
        if (link?.send(message) != true) {
            tv?.let(::connect)
        }
    }

    private fun toast(message: String) = Toast.makeText(this, message, Toast.LENGTH_SHORT).show()

    // ---- controle ----

    private fun showRemote(target: Tv) {
        val content = LinearLayout(this)

        val header = LinearLayout(this).apply { gravity = Gravity.CENTER_VERTICAL }
        val title = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        title.addView(label(target.name, 20f, bold = true))
        status = label("", 13f, mutedColor).apply { setOnClickListener { tv?.let(::connect) } }
        title.addView(status)
        header.addView(title, LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
        header.addView(button("Trocar TV") { showPicker() }, LinearLayout.LayoutParams(WRAP_CONTENT, dp(44)))
        content.addView(header)

        content.addView(
            row(
                button("Desktop") { send(Protocol.mode("desktop")) },
                button("Gaming") { send(Protocol.mode("gaming")) },
                button("Mídia") { send(Protocol.mode("media")) },
            ).apply { setPadding(0, dp(16), 0, 0) },
        )

        val pad = DPadView(this) { action -> send(Protocol.pad(action)) }
        content.addView(pad, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply { setMargins(dp(24), dp(16), dp(24), dp(12)) })

        content.addView(
            row(
                button("Voltar") { send(Protocol.pad("back")) },
                button("Início", primary = true) { send(Protocol.home()) },
                button("Menu") { send(Protocol.pad("menu")) },
            ),
        )
        content.addView(
            row(
                button("−10 s") { send(Protocol.media("back")) },
                button("Play/Pausa", primary = true) { send(Protocol.media("play-pause")) },
                button("+10 s") { send(Protocol.media("forward")) },
            ),
        )
        content.addView(
            row(
                button("Vol −") { send(Protocol.volume("down")) },
                button("Mudo") { send(Protocol.volume("mute")) },
                button("Vol +") { send(Protocol.volume("up")) },
            ),
        )

        val typing = EditText(this).apply {
            hint = "Digitar na TV"
            setTextColor(textColor)
            setHintTextColor(mutedColor)
            imeOptions = EditorInfo.IME_ACTION_SEND
            inputType = InputType.TYPE_CLASS_TEXT
            setSingleLine()
            setOnEditorActionListener { view, _, _ ->
                val value = (view as EditText).text.toString()
                if (value.isNotEmpty()) {
                    send(Protocol.text(value))
                    view.setText("")
                }
                true
            }
        }
        content.addView(typing, LinearLayout.LayoutParams(MATCH_PARENT, dp(56)).apply { topMargin = dp(10) })
        content.addView(
            row(
                button("Apagar") { send(Protocol.key("backspace")) },
                button("Enter") { send(Protocol.key("enter")) },
            ),
        )

        setContentView(page(content))
    }

    @Deprecated("Activity.onBackPressed: aqui o voltar do celular volta para a lista de TVs.")
    override fun onBackPressed() {
        if (tv != null) showPicker() else super.onBackPressed()
    }
}
