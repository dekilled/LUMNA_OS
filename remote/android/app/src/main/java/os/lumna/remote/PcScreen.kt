package os.lumna.remote

import android.app.AlertDialog
import android.app.TimePickerDialog
import android.text.InputType
import android.view.View
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.widget.CheckBox
import android.widget.EditText
import android.widget.LinearLayout
import java.text.SimpleDateFormat
import java.util.Calendar
import java.util.Date
import java.util.Locale

/** O PC visto de longe: como ele está, energia (bloquear, suspender, reiniciar, desligar) e agendamentos. */
class PcScreen(host: Host) : Screen(host) {
    private val infoCard = ui.card()
    private val controlNote = ui.card()
    private val tasksBox = ui.column()
    override val view: View

    private val actions = listOf(
        "remind" to "Lembrete",
        "emma" to "Pedir à Emma",
        "lock" to "Bloquear o PC",
        "suspend" to "Suspender o PC",
        "reboot" to "Reiniciar o PC",
        "poweroff" to "Desligar o PC",
    )

    init {
        val content = ui.column()
        val header = LinearLayout(host.activity)
        header.addView(ui.title("Meu PC", "Como ele está e o que ele pode fazer a uma hora marcada."), LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
        header.addView(ui.button("Atualizar") { Connect.refreshPc() }, LinearLayout.LayoutParams(WRAP_CONTENT, ui.dp(44)))
        content.addView(header)

        content.addView(infoCard)
        controlNote.addView(ui.label("O controle de longe está desligado para este celular no PC. Ligue em Painel > LUMNA Connect.", 14f, ui.danger))
        content.addView(controlNote)

        content.addView(ui.section("Energia"))
        content.addView(
            ui.row(
                ui.button("Bloquear") { power("lock", null) },
                ui.button("Suspender") { power("suspend", "Suspender o PC agora?") },
            ),
        )
        content.addView(
            ui.row(
                ui.button("Reiniciar") { power("reboot", "Reiniciar o PC agora? O que não foi salvo se perde.") },
                ui.button("Desligar", warning = true) { power("poweroff", "Desligar o PC agora? O que não foi salvo se perde.") },
            ),
        )

        content.addView(ui.section("Agendamentos"))
        content.addView(tasksBox)
        content.addView(ui.button("Novo agendamento", primary = true) { chooseAction() }, LinearLayout.LayoutParams(MATCH_PARENT, ui.dp(52)).apply { topMargin = ui.dp(8) })
        view = ui.page(content)
        render()
    }

    override fun onShow() {
        Connect.refreshPc()
        render()
    }

    override fun onEvent(event: Connect.Event) {
        when (event) {
            is Connect.Event.Info, is Connect.Event.Schedule, is Connect.Event.Features, is Connect.Event.Status -> render()
            else -> {}
        }
    }

    private fun power(action: String, question: String?) {
        val go = {
            if (Connect.send(Protocol.power(action))) Connect.toast("Pedido enviado ao PC") else Connect.toast("Sem conexão com o PC")
        }
        if (question == null) return go()
        AlertDialog.Builder(host.activity).setMessage(question).setPositiveButton("Sim") { _, _ -> go() }.setNegativeButton("Não", null).show()
    }

    // ---- agendar ----

    private fun chooseAction() {
        AlertDialog.Builder(host.activity)
            .setTitle("O que o PC deve fazer?")
            .setItems(actions.map { it.second }.toTypedArray()) { _, i -> chooseTime(actions[i].first) }
            .show()
    }

    private fun chooseTime(action: String) {
        val now = Calendar.getInstance()
        TimePickerDialog(host.activity, { _, hour, minute ->
            val at = Calendar.getInstance().apply {
                set(Calendar.HOUR_OF_DAY, hour)
                set(Calendar.MINUTE, minute)
                set(Calendar.SECOND, 0)
                set(Calendar.MILLISECOND, 0)
                if (timeInMillis <= System.currentTimeMillis()) add(Calendar.DAY_OF_YEAR, 1)
            }
            details(action, at.timeInMillis / 1000)
        }, now.get(Calendar.HOUR_OF_DAY), now.get(Calendar.MINUTE), true).show()
    }

    private fun details(action: String, at: Long) {
        val needsText = action == "remind" || action == "emma"
        val box = ui.column().apply { setPadding(ui.dp(20), ui.dp(8), ui.dp(20), 0) }
        val text = EditText(host.activity).apply {
            hint = if (action == "emma") "O que a Emma deve fazer" else "Texto do lembrete"
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_CAP_SENTENCES
        }
        if (needsText) box.addView(text)
        val daily = CheckBox(host.activity).apply { setText("Repetir todo dia") }
        box.addView(daily)
        val label = actions.first { it.first == action }.second
        AlertDialog.Builder(host.activity)
            .setTitle("$label · ${formatTime(at)}")
            .setView(box)
            .setPositiveButton("Agendar") { _, _ ->
                val value = text.text.toString().trim()
                if (needsText && value.isEmpty()) {
                    Connect.toast("Escreva o texto")
                } else if (!Connect.send(Protocol.scheduleAdd(action, at, value, daily.isChecked))) {
                    Connect.toast("Sem conexão com o PC")
                }
            }
            .setNegativeButton("Cancelar", null)
            .show()
    }

    // ---- mostrar ----

    private fun formatTime(seconds: Long): String = SimpleDateFormat("EEE dd/MM HH:mm", Locale("pt", "BR")).format(Date(seconds * 1000))

    private fun duration(seconds: Long): String {
        val days = seconds / 86400
        val hours = seconds % 86400 / 3600
        val minutes = seconds % 3600 / 60
        return if (days > 0) "$days d $hours h" else if (hours > 0) "$hours h $minutes min" else "$minutes min"
    }

    private fun render() {
        controlNote.visibility = if (Connect.connected && !Connect.feature("control")) View.VISIBLE else View.GONE
        infoCard.removeAllViews()
        val info = Connect.info
        if (info == null) {
            infoCard.addView(ui.label(if (Connect.connected) "Pedindo as informações…" else "Sem conexão com o PC.", 14f, ui.muted))
        } else {
            infoCard.addView(ui.label(info.host.ifEmpty { Connect.pc?.name ?: "PC" }, 20f, bold = true))
            if (info.user.isNotEmpty()) infoCard.addView(ui.label("Usuário ${info.user}", 13f, ui.muted))
            infoCard.addView(ui.label("Ligado há ${duration(info.uptime)}", 14f).apply { setPadding(0, ui.dp(8), 0, 0) })
            if (info.memoryTotalKb > 0) {
                val used = (info.memoryTotalKb - info.memoryAvailableKb) * 100 / info.memoryTotalKb
                infoCard.addView(ui.label("Memória em uso: $used% de ${info.memoryTotalKb / 1024 / 1024} GB", 14f))
            }
            info.load?.let { infoCard.addView(ui.label("Carga do processador: $it", 14f)) }
            info.battery?.let { infoCard.addView(ui.label("Bateria: $it%" + if (info.charging) " (carregando)" else "", 14f)) }
        }

        tasksBox.removeAllViews()
        if (Connect.tasks.isEmpty()) {
            tasksBox.addView(ui.label("Nenhum agendamento.", 14f, ui.muted).apply { setPadding(ui.dp(4), 0, 0, ui.dp(4)) })
        }
        for (task in Connect.tasks) {
            val card = ui.card()
            val name = actions.firstOrNull { it.first == task.action }?.second ?: task.action
            card.addView(ui.label(name, 16f, bold = true))
            card.addView(ui.label(formatTime(task.at) + if (task.daily) " · todo dia" else "", 14f, ui.violetSoft))
            if (task.text.isNotEmpty()) card.addView(ui.label(task.text, 14f))
            if (task.by.isNotEmpty()) card.addView(ui.label("por ${task.by}", 12f, ui.muted))
            card.addView(ui.button("Remover") { Connect.send(Protocol.scheduleRemove(task.id)) }, LinearLayout.LayoutParams(MATCH_PARENT, ui.dp(44)).apply { topMargin = ui.dp(8) })
            tasksBox.addView(card)
        }
    }
}
