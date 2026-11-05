package os.lumna.remote

import android.view.View
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.widget.LinearLayout

/** Compartilhar com o PC: arquivos nos dois sentidos e a área de transferência. */
class ShareScreen(host: Host) : Screen(host) {
    private val transfersBox = ui.column()
    private val clipsBox = ui.column()
    override val view: View

    init {
        val content = ui.column()
        content.addView(ui.title("Compartilhar", "Arquivos e texto entre o celular e o PC."))

        content.addView(ui.section("Arquivos"))
        content.addView(ui.button("Enviar arquivos para o PC", primary = true) { host.pickFiles() }, LinearLayout.LayoutParams(MATCH_PARENT, ui.dp(56)))
        content.addView(
            ui.label("Também dá para enviar de qualquer app: Compartilhar > LUMNA Connect. Os arquivos que o PC enviar chegam sozinhos em Downloads/LUMNA.", 13f, ui.muted)
                .apply { setPadding(ui.dp(4), ui.dp(8), 0, ui.dp(8)) },
        )
        content.addView(transfersBox)

        content.addView(ui.section("Área de transferência"))
        content.addView(ui.button("Enviar o que copiei para o PC") { Connect.sendClipboard() }, LinearLayout.LayoutParams(MATCH_PARENT, ui.dp(56)))
        content.addView(
            ui.label("O que você copiar no PC chega aqui sozinho. Do celular para o PC, toque no botão (ou ligue o envio automático em Ajustes).", 13f, ui.muted)
                .apply { setPadding(ui.dp(4), ui.dp(8), 0, ui.dp(8)) },
        )
        content.addView(clipsBox)
        view = ui.page(content)
        render()
    }

    override fun onShow() = render()

    override fun onEvent(event: Connect.Event) {
        when (event) {
            is Connect.Event.Transfers, is Connect.Event.Clips -> render()
            else -> {}
        }
    }

    private fun size(bytes: Long): String = when {
        bytes >= 1L shl 30 -> "%.1f GB".format(bytes / (1L shl 30).toDouble())
        bytes >= 1L shl 20 -> "%.1f MB".format(bytes / (1L shl 20).toDouble())
        bytes >= 1L shl 10 -> "%d KB".format(bytes shr 10)
        else -> "$bytes B"
    }

    private fun render() {
        transfersBox.removeAllViews()
        for (t in Connect.transfers.take(8)) {
            val card = ui.card()
            card.addView(ui.label((if (t.outgoing) "↑ " else "↓ ") + t.name, 15f, bold = true))
            val line = when (t.state) {
                "done" -> "Pronto · ${size(t.size)}"
                "error" -> "Falhou: ${t.error}"
                else -> if (t.size > 0) "${t.done * 100 / t.size}% · ${size(t.done)} de ${size(t.size)}" else "Enviando…"
            }
            card.addView(ui.label(line, 13f, if (t.state == "error") ui.danger else if (t.state == "done") ui.good else ui.muted))
            transfersBox.addView(card)
        }

        clipsBox.removeAllViews()
        for (clip in Connect.clips.take(5)) {
            val card = ui.card()
            card.addView(ui.label(if (clip.length > 160) clip.take(160) + "…" else clip, 14f))
            card.addView(ui.button("Copiar de novo") { Connect.copyLocally(clip); Connect.toast("Copiado") }, LinearLayout.LayoutParams(MATCH_PARENT, ui.dp(44)).apply { topMargin = ui.dp(8) })
            clipsBox.addView(card)
        }
    }
}
