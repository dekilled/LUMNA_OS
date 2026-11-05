package os.lumna.remote

import android.app.Activity
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.view.Gravity
import android.view.HapticFeedbackConstants
import android.view.View
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.Switch
import android.widget.TextView

/** O que as telas pedem à atividade que as mostra (só ela sabe abrir seletores e o reconhecimento de voz). */
interface Host {
    val activity: Activity
    fun pickFiles()
    fun listen(onText: (String) -> Unit)
    fun showPicker()
    fun requestNotificationPermission()
    fun openListenerSettings()
    fun setKeepAlive(on: Boolean)
}

/** Uma tela do app: uma vista que se refaz quando o estado da conexão muda. */
abstract class Screen(protected val host: Host) {
    protected val ui = Ui(host.activity)
    abstract val view: View
    open fun onEvent(event: Connect.Event) {}
    /** A tela foi para a frente. */
    open fun onShow() {}
}

/** Os blocos de interface do app, no estilo do LUMNA (violeta, cantos redondos), sem layouts em XML. */
class Ui(private val activity: Activity) {
    val violet = Color.parseColor("#8B5CF6")
    val violetSoft = Color.parseColor("#A78BFA")
    val surface = Color.parseColor("#1A1433")
    val textColor = Color.parseColor("#ECE8FF")
    val muted = Color.parseColor("#A59CC9")
    val danger = Color.parseColor("#F87171")
    val good = Color.parseColor("#34D399")

    fun dp(value: Int) = (value * activity.resources.displayMetrics.density).toInt()

    fun label(value: String, size: Float = 15f, color: Int = textColor, bold: Boolean = false) = TextView(activity).apply {
        text = value
        textSize = size
        setTextColor(color)
        if (bold) typeface = Typeface.DEFAULT_BOLD
    }

    /** O título pequeno de uma seção. */
    fun section(value: String) = label(value.uppercase(), 12f, muted, bold = true).apply {
        letterSpacing = 0.08f
        setPadding(dp(4), dp(16), 0, dp(8))
    }

    fun button(value: String, primary: Boolean = false, warning: Boolean = false, onClick: () -> Unit) = Button(activity).apply {
        text = value
        isAllCaps = false
        textSize = 15f
        setTextColor(Color.WHITE)
        background = GradientDrawable().apply {
            cornerRadius = dp(16).toFloat()
            setColor(if (warning) Color.parseColor("#7F1D1D") else if (primary) violet else surface)
        }
        stateListAnimator = null
        setOnClickListener {
            it.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
            onClick()
        }
    }

    fun column() = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL }

    /** Botões lado a lado, do mesmo tamanho. */
    fun row(vararg views: View, height: Int = 56) = LinearLayout(activity).apply {
        orientation = LinearLayout.HORIZONTAL
        for (v in views) addView(v, LinearLayout.LayoutParams(0, dp(height), 1f).apply { setMargins(dp(5), dp(5), dp(5), dp(5)) })
    }

    /** Um bloco com fundo, onde as informações de uma seção moram. */
    fun card() = LinearLayout(activity).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(dp(16), dp(14), dp(16), dp(14))
        background = GradientDrawable().apply {
            cornerRadius = dp(18).toFloat()
            setColor(surface)
        }
        layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply { bottomMargin = dp(10) }
    }

    fun add(parent: LinearLayout, child: View, topMargin: Int = 0, height: Int = WRAP_CONTENT) {
        parent.addView(child, LinearLayout.LayoutParams(MATCH_PARENT, height).apply { this.topMargin = dp(topMargin) })
    }

    /** Uma linha de ajuste com chave de liga/desliga. */
    fun switchRow(title: String, subtitle: String, checked: Boolean, onChange: (Boolean) -> Unit): View {
        val row = LinearLayout(activity).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(0, dp(8), 0, dp(8))
        }
        val texts = column()
        texts.addView(label(title, 15f))
        if (subtitle.isNotEmpty()) texts.addView(label(subtitle, 12f, muted))
        row.addView(texts, LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
        val toggle = Switch(activity).apply {
            isChecked = checked
            setOnCheckedChangeListener { _, on -> onChange(on) }
        }
        row.addView(toggle, LinearLayout.LayoutParams(WRAP_CONTENT, WRAP_CONTENT))
        return row
    }

    /** Uma tela que rola, com as margens do app. */
    fun page(content: LinearLayout): View = ScrollView(activity).apply {
        isFillViewport = true
        addView(content.apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(20), dp(16), dp(16))
        })
    }

    fun title(value: String, subtitle: String = ""): LinearLayout = column().apply {
        addView(label(value, 26f, bold = true))
        if (subtitle.isNotEmpty()) addView(label(subtitle, 14f, muted).apply { setPadding(0, dp(4), 0, dp(8)) })
    }
}
