package os.lumna.remote

import org.json.JSONObject
import java.util.Base64

/**
 * Mensagens do protocolo do LUMNA Connect (o lumna-connect do PC: gravity/exemplos/connect.c).
 * Uma mensagem JSON por quadro de texto do WebSocket.
 */
object Protocol {
    const val PORT = 47800
    const val SERVICE_TYPE = "_lumna-remote._tcp."

    /** O tamanho de cada pedaço de arquivo (o PC aceita o mesmo). */
    const val CHUNK = 192 * 1024

    fun pair(code: String, name: String): String =
        JSONObject().put("type", "pair").put("code", code).put("name", name).toString()

    fun hello(token: String): String = JSONObject().put("type", "hello").put("token", token).toString()

    /** Ações do controle: up, down, left, right, accept, back, menu, guide. */
    fun pad(action: String): String = JSONObject().put("type", "pad").put("action", action).toString()

    fun text(value: String): String = JSONObject().put("type", "text").put("value", value).toString()

    /** Teclas especiais: backspace, enter, escape, space. */
    fun key(value: String): String = JSONObject().put("type", "key").put("value", value).toString()

    /** play-pause, next, previous, forward (+10 s), back (-10 s). */
    fun media(action: String): String = JSONObject().put("type", "media").put("action", action).toString()

    /** up, down, mute. */
    fun volume(action: String): String = JSONObject().put("type", "volume").put("action", action).toString()

    /** desktop, gaming, media. */
    fun mode(value: String): String = JSONObject().put("type", "mode").put("value", value).toString()

    fun home(): String = JSONObject().put("type", "home").toString()

    /** O que o celular entendeu da fala: a Emma do PC age (abre a Netflix, pausa...) e responde com [Reply.Voice]. */
    fun voice(text: String): String = JSONObject().put("type", "voice").put("text", text).toString()

    /** Liga (ou encerra) o microfone do PC: a fala é transcrita lá. */
    fun voiceListen(): String = JSONObject().put("type", "voice").put("action", "listen").toString()

    // ---- o PC visto de longe ----

    fun info(): String = JSONObject().put("type", "info").toString()

    /** lock, suspend, reboot, poweroff. */
    fun power(action: String): String = JSONObject().put("type", "power").put("action", action).toString()

    fun scheduleList(): String = JSONObject().put("type", "schedule-list").toString()

    fun scheduleAdd(action: String, at: Long, text: String, daily: Boolean): String =
        JSONObject().put("type", "schedule-add").put(
            "task",
            JSONObject().put("action", action).put("at", at).put("text", text).put("daily", daily),
        ).toString()

    fun scheduleRemove(id: String): String = JSONObject().put("type", "schedule-remove").put("id", id).toString()

    /** Uma mensagem de texto para a Emma do PC; a resposta chega depois, como [Reply.EmmaReply]. */
    fun emma(text: String): String = JSONObject().put("type", "emma").put("text", text).toString()

    fun features(): String = JSONObject().put("type", "features").toString()

    // ---- área de transferência, avisos e arquivos ----

    fun clipboard(text: String): String = JSONObject().put("type", "clipboard").put("text", text).toString()

    /** Um aviso do celular para aparecer no PC. */
    fun notify(app: String, summary: String, body: String, urgent: Boolean): String =
        JSONObject().put("type", "notify").put("app", app).put("summary", summary).put("body", body).put("urgent", urgent).toString()

    fun fileBegin(id: String, name: String, size: Long): String =
        JSONObject().put("type", "file-begin").put("id", id).put("name", name).put("size", size).toString()

    fun fileChunk(id: String, bytes: ByteArray, length: Int): String =
        JSONObject().put("type", "file-chunk").put("id", id)
            .put("data", Base64.getEncoder().encodeToString(bytes.copyOf(length))).toString()

    fun fileEnd(id: String): String = JSONObject().put("type", "file-end").put("id", id).toString()

    fun fileCancel(id: String): String = JSONObject().put("type", "file-cancel").put("id", id).toString()

    fun filePull(id: String, offset: Long): String = JSONObject().put("type", "file-pull").put("id", id).put("offset", offset).toString()

    fun fileReject(id: String, reason: String): String =
        JSONObject().put("type", "file-reject").put("id", id).put("reason", reason).toString()

    // ---- o que o PC responde ----

    /** Um agendamento do PC (o mesmo formato do schedule.json). */
    data class Task(val id: String, val at: Long, val action: String, val text: String, val daily: Boolean, val by: String)

    /** Resposta do PC, já interpretada. */
    sealed class Reply {
        data class Paired(val token: String, val pcName: String) : Reply()
        data class Ready(val pcName: String) : Reply()
        data class Volume(val value: Int?, val muted: Boolean) : Reply()
        /** A resposta da Emma a uma fala ([ok] falso: ela não conseguiu; [text] diz por quê ou o que fez). */
        data class Voice(val text: String, val ok: Boolean) : Reply()
        data class Info(
            val host: String,
            val user: String,
            val uptime: Long,
            val load: String?,
            val memoryTotalKb: Long,
            val memoryAvailableKb: Long,
            val battery: Int?,
            val charging: Boolean,
        ) : Reply()
        data class Schedule(val tasks: List<Task>) : Reply()
        data class EmmaReply(val content: String) : Reply()
        /** O que está ligado para este celular no PC (notifications, phone_notifications, clipboard, files, control). */
        data class Features(val on: Map<String, Boolean>) : Reply()
        data class Clipboard(val text: String) : Reply()
        /** Um aviso do PC para aparecer no celular. */
        data class Notification(val app: String, val summary: String, val body: String, val urgent: Boolean) : Reply()
        /** O PC oferece um arquivo; o celular puxa com [filePull]. */
        data class FileOffer(val id: String, val name: String, val size: Long) : Reply()
        class FileData(val bytes: ByteArray, val eof: Boolean) : Reply()
        data class Error(val message: String, val unpaired: Boolean) : Reply()
        object Ok : Reply()
        /** Algo que o app não usa (o modo da Mídia, por exemplo). */
        data class Other(val type: String) : Reply()
    }

    fun parse(text: String): Reply {
        val json = runCatching { JSONObject(text) }.getOrNull() ?: return Reply.Error("resposta inválida do PC", false)
        return when (val type = json.optString("type")) {
            "paired" -> Reply.Paired(json.getString("token"), json.optString("name", "LUMNA"))
            "ready" -> Reply.Ready(json.optString("name", "LUMNA"))
            "volume" -> Reply.Volume(if (json.isNull("value")) null else json.optInt("value"), json.optBoolean("muted"))
            "voice_reply" -> Reply.Voice(json.optString("text", json.optString("error")), json.optBoolean("ok"))
            "error" -> Reply.Error(json.optString("message", "erro"), json.optBoolean("unpaired"))
            "info" -> Reply.Info(
                host = json.optString("host"),
                user = json.optString("user"),
                uptime = json.optLong("uptime"),
                load = if (json.isNull("load")) null else json.optString("load"),
                memoryTotalKb = json.optLong("memory_total_kb"),
                memoryAvailableKb = json.optLong("memory_available_kb"),
                battery = if (json.isNull("battery")) null else json.optInt("battery"),
                charging = json.optBoolean("charging"),
            )
            "schedule" -> {
                val list = json.optJSONArray("tasks")
                val tasks = (0 until (list?.length() ?: 0)).mapNotNull { i ->
                    list?.optJSONObject(i)?.let {
                        Task(it.optString("id"), it.optLong("at"), it.optString("action"), it.optString("text"), it.optBoolean("daily"), it.optString("by"))
                    }
                }
                Reply.Schedule(tasks)
            }
            "emma-reply" -> Reply.EmmaReply(json.optString("content"))
            "features" -> {
                val map = mutableMapOf<String, Boolean>()
                json.optJSONObject("features")?.let { f -> f.keys().forEach { k -> map[k] = f.optBoolean(k) } }
                Reply.Features(map)
            }
            "clipboard" -> Reply.Clipboard(json.optString("text"))
            "notification" -> Reply.Notification(
                json.optString("app"), json.optString("summary"), json.optString("body"), json.optBoolean("urgent"),
            )
            "file-offer" -> Reply.FileOffer(json.optString("id"), json.optString("name"), json.optLong("size"))
            "file-data" -> {
                val bytes = runCatching { Base64.getDecoder().decode(json.optString("data")) }.getOrNull()
                    ?: return Reply.Error("pedaço de arquivo inválido", false)
                Reply.FileData(bytes, json.optBoolean("eof"))
            }
            "ok" -> Reply.Ok
            else -> Reply.Other(type)
        }
    }
}
