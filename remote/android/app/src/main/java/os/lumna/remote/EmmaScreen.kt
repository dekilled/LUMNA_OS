package os.lumna.remote

import android.graphics.drawable.GradientDrawable
import android.text.InputType
import android.view.Gravity
import android.view.View
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView

/** Conversar com a Emma do PC: por texto ou pela voz do celular. */
class EmmaScreen(host: Host) : Screen(host) {
    private val messages = ui.column()
    private val scroll = ScrollView(host.activity)
    private val input = EditText(host.activity)
    override val view: View

    init {
        val root = ui.column().apply { setPadding(ui.dp(16), ui.dp(20), ui.dp(16), ui.dp(8)) }
        val header = LinearLayout(host.activity).apply { gravity = Gravity.CENTER_VERTICAL }
        header.addView(ui.title("Emma", "Pede para ela abrir, procurar ou resolver no PC."), LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
        header.addView(ui.button("Nova") { Connect.newChat() }, LinearLayout.LayoutParams(WRAP_CONTENT, ui.dp(44)))
        root.addView(header)

        scroll.addView(messages.apply { setPadding(0, ui.dp(4), 0, ui.dp(4)) })
        root.addView(scroll, LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f))

        input.apply {
            hint = "Escreva para a Emma"
            setTextColor(ui.textColor)
            setHintTextColor(ui.muted)
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE or InputType.TYPE_TEXT_FLAG_CAP_SENTENCES
            maxLines = 4
        }
        val bar = LinearLayout(host.activity).apply { gravity = Gravity.CENTER_VERTICAL }
        bar.addView(input, LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
        bar.addView(ui.button("🎤") { host.listen { said -> Connect.say(said) } }, LinearLayout.LayoutParams(ui.dp(56), ui.dp(48)).apply { marginStart = ui.dp(6) })
        bar.addView(ui.button("Enviar", primary = true) { send() }, LinearLayout.LayoutParams(WRAP_CONTENT, ui.dp(48)).apply { marginStart = ui.dp(6) })
        root.addView(bar, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT))
        view = root
        render()
    }

    private fun send() {
        val text = input.text.toString().trim()
        if (text.isEmpty()) return
        input.setText("")
        Connect.askEmma(text)
    }

    override fun onShow() = render()

    override fun onEvent(event: Connect.Event) {
        if (event is Connect.Event.Chat) render()
    }

    private fun bubble(text: String, mine: Boolean, error: Boolean): View {
        val tv = ui.label(text, 15f, if (error) ui.danger else ui.textColor)
        tv.setTextIsSelectable(true)
        tv.setPadding(ui.dp(14), ui.dp(10), ui.dp(14), ui.dp(10))
        tv.background = GradientDrawable().apply {
            cornerRadius = ui.dp(16).toFloat()
            setColor(if (mine) ui.violet else ui.surface)
        }
        tv.maxWidth = (host.activity.resources.displayMetrics.widthPixels * 0.82f).toInt()
        val wrap = LinearLayout(host.activity).apply { gravity = if (mine) Gravity.END else Gravity.START }
        wrap.addView(tv, LinearLayout.LayoutParams(WRAP_CONTENT, WRAP_CONTENT))
        wrap.layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply { topMargin = ui.dp(6) }
        return wrap
    }

    private fun render() {
        messages.removeAllViews()
        if (Connect.chat.isEmpty() && !Connect.emmaBusy) {
            messages.addView(ui.label("Nada por aqui ainda. Escreva abaixo ou toque no microfone.", 14f, ui.muted).apply { setPadding(ui.dp(4), ui.dp(12), 0, 0) })
        }
        for (line in Connect.chat) messages.addView(bubble(line.text, line.fromMe, line.error))
        if (Connect.emmaBusy) messages.addView(ui.label("A Emma está pensando…", 13f, ui.muted).apply { setPadding(ui.dp(4), ui.dp(8), 0, 0) })
        scroll.post { scroll.fullScroll(View.FOCUS_DOWN) }
    }
}
