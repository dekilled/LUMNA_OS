package os.lumna.remote

import android.text.InputType
import android.view.Gravity
import android.view.View
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.view.inputmethod.EditorInfo
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.TextView

/** O controle remoto: direcional, voltar, início, volume, mídia, modos e teclado. */
class ControlScreen(host: Host) : Screen(host) {
    private val status: TextView
    private val name: TextView
    override val view: View

    init {
        val content = ui.column()

        val header = LinearLayout(host.activity).apply { gravity = Gravity.CENTER_VERTICAL }
        val title = ui.column()
        name = ui.label(Connect.pc?.name ?: "", 20f, bold = true)
        status = ui.label(Connect.status, 13f, ui.muted).apply { setOnClickListener { Connect.pc?.let(Connect::connect) } }
        title.addView(name)
        title.addView(status)
        header.addView(title, LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
        header.addView(ui.button("Trocar PC") { host.showPicker() }, LinearLayout.LayoutParams(WRAP_CONTENT, ui.dp(44)))
        content.addView(header)

        content.addView(
            ui.row(
                ui.button("Desktop") { Connect.send(Protocol.mode("desktop")) },
                ui.button("Gaming") { Connect.send(Protocol.mode("gaming")) },
                ui.button("Mídia") { Connect.send(Protocol.mode("media")) },
            ).apply { setPadding(0, ui.dp(12), 0, 0) },
        )

        val pad = DPadView(host.activity) { action -> Connect.send(Protocol.pad(action)) }
        content.addView(pad, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply { setMargins(ui.dp(24), ui.dp(12), ui.dp(24), ui.dp(8)) })

        content.addView(
            ui.row(
                ui.button("Falar com a Emma", primary = true) { host.listen { said -> Connect.say(said) } },
                ui.button("Microfone do PC") { Connect.send(Protocol.voiceListen()) },
            ),
        )
        content.addView(
            ui.row(
                ui.button("Voltar") { Connect.send(Protocol.pad("back")) },
                ui.button("Início", primary = true) { Connect.send(Protocol.home()) },
                ui.button("Menu") { Connect.send(Protocol.pad("menu")) },
            ),
        )
        content.addView(
            ui.row(
                ui.button("−10 s") { Connect.send(Protocol.media("back")) },
                ui.button("Play/Pausa", primary = true) { Connect.send(Protocol.media("play-pause")) },
                ui.button("+10 s") { Connect.send(Protocol.media("forward")) },
            ),
        )
        content.addView(
            ui.row(
                ui.button("Vol −") { Connect.send(Protocol.volume("down")) },
                ui.button("Mudo") { Connect.send(Protocol.volume("mute")) },
                ui.button("Vol +") { Connect.send(Protocol.volume("up")) },
            ),
        )

        val typing = EditText(host.activity).apply {
            hint = "Digitar no PC"
            setTextColor(ui.textColor)
            setHintTextColor(ui.muted)
            imeOptions = EditorInfo.IME_ACTION_SEND
            inputType = InputType.TYPE_CLASS_TEXT
            setSingleLine()
            setOnEditorActionListener { v, _, _ ->
                val value = (v as EditText).text.toString()
                if (value.isNotEmpty()) {
                    Connect.send(Protocol.text(value))
                    v.setText("")
                }
                true
            }
        }
        content.addView(typing, LinearLayout.LayoutParams(MATCH_PARENT, ui.dp(56)).apply { topMargin = ui.dp(8) })
        content.addView(
            ui.row(
                ui.button("Apagar") { Connect.send(Protocol.key("backspace")) },
                ui.button("Enter") { Connect.send(Protocol.key("enter")) },
            ),
        )
        view = ui.page(content)
    }

    override fun onShow() = render()

    override fun onEvent(event: Connect.Event) {
        if (event is Connect.Event.Status) render()
    }

    private fun render() {
        name.text = Connect.pc?.name ?: ""
        status.text = if (Connect.connected) Connect.status else "${Connect.status} · toque para reconectar"
    }
}
