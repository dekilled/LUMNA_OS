package os.lumna.remote

import android.app.AlertDialog
import android.view.View
import android.view.ViewGroup.LayoutParams.MATCH_PARENT

/** Ajustes: o PC conectado, o que está ligado nele para este celular e as permissões do app. */
class SettingsScreen(host: Host) : Screen(host) {
    private val pcCard = ui.card()
    private val featuresCard = ui.card()
    private val appCard = ui.card()
    override val view: View

    private val featureNames = listOf(
        "notifications" to "Avisos do PC no celular",
        "phone_notifications" to "Avisos do celular no PC",
        "clipboard" to "Área de transferência",
        "files" to "Arquivos",
        "control" to "Controle de longe (energia, agendamentos, Emma)",
    )

    init {
        val content = ui.column()
        content.addView(ui.title("Ajustes"))
        content.addView(ui.section("PC conectado"))
        content.addView(pcCard)
        content.addView(ui.section("O que o PC permite a este celular"))
        content.addView(featuresCard)
        content.addView(ui.section("Neste celular"))
        content.addView(appCard)
        content.addView(ui.label("LUMNA Connect · o mesmo app controla o PC, troca arquivos e avisos e fala com a Emma.", 12f, ui.muted).apply { setPadding(ui.dp(4), ui.dp(16), 0, 0) })
        view = ui.page(content)
        render()
    }

    override fun onShow() = render()

    override fun onEvent(event: Connect.Event) {
        when (event) {
            is Connect.Event.Status, is Connect.Event.Features -> render()
            else -> {}
        }
    }

    private fun render() {
        pcCard.removeAllViews()
        val pc = Connect.pc
        pcCard.addView(ui.label(pc?.name ?: "Nenhum PC", 18f, bold = true))
        pcCard.addView(ui.label(Connect.status, 13f, if (Connect.connected) ui.good else ui.muted))
        pcCard.addView(ui.row(
            ui.button("Trocar PC") { host.showPicker() },
            ui.button("Esquecer", warning = true) { forget() },
            height = 48,
        ))

        featuresCard.removeAllViews()
        if (!Connect.connected || Connect.features.isEmpty()) {
            featuresCard.addView(ui.label("Conecte ao PC para ver.", 14f, ui.muted))
        } else {
            for ((key, name) in featureNames) {
                val on = Connect.feature(key)
                featuresCard.addView(ui.label((if (on) "✓  " else "✗  ") + name, 14f, if (on) ui.textColor else ui.muted).apply { setPadding(0, ui.dp(3), 0, ui.dp(3)) })
            }
            featuresCard.addView(ui.label("Para mudar, abra Painel > LUMNA Connect no PC.", 12f, ui.muted).apply { setPadding(0, ui.dp(6), 0, 0) })
        }

        appCard.removeAllViews()
        appCard.addView(ui.switchRow("Manter conectado", "Um aviso fixo mantém a conexão com o app fechado, para os avisos e arquivos chegarem.", Connect.store.keepAlive) { on ->
            Connect.store.keepAlive = on
            host.setKeepAlive(on)
        })
        appCard.addView(ui.switchRow("Enviar o que eu copiar", "Ao abrir o app, o texto copiado vai para o PC. O Android só deixa ler a cópia com o app na frente.", Connect.store.autoClipboard) { on ->
            Connect.store.autoClipboard = on
        })
        appCard.addView(ui.switchRow("Avisos do celular no PC", if (PhoneNotifications.allowed(host.activity)) "Permissão dada." else "Falta dar acesso às notificações.", Connect.store.mirrorPhone) { on ->
            Connect.store.mirrorPhone = on
            if (on && !PhoneNotifications.allowed(host.activity)) host.openListenerSettings()
        })
        if (!PhoneNotifications.allowed(host.activity)) {
            appCard.addView(ui.button("Dar acesso às notificações") { host.openListenerSettings() }, android.widget.LinearLayout.LayoutParams(MATCH_PARENT, ui.dp(48)))
        }
        if (!Notifier.canPost(host.activity)) {
            appCard.addView(ui.button("Permitir avisos do PC aqui") { host.requestNotificationPermission() }, android.widget.LinearLayout.LayoutParams(MATCH_PARENT, ui.dp(48)).apply { topMargin = ui.dp(6) })
        }
    }

    private fun forget() {
        val pc = Connect.pc ?: return
        AlertDialog.Builder(host.activity)
            .setMessage("Esquecer ${pc.name}? Para voltar, será preciso parear de novo com o código.")
            .setPositiveButton("Esquecer") { _, _ ->
                Connect.store.forget(pc)
                Connect.disconnect()
                host.showPicker()
            }
            .setNegativeButton("Cancelar", null)
            .show()
    }
}
