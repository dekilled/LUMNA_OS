package os.lumna.remote

import org.json.JSONObject

/**
 * Mensagens do protocolo do LUMNA Controle (ver shell/src-tauri/src/remote.rs no repositório).
 * Uma mensagem JSON por quadro de texto do WebSocket.
 */
object Protocol {
    const val PORT = 47800
    const val SERVICE_TYPE = "_lumna-remote._tcp."

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

    /** O que o celular entendeu da fala: a Emma da TV age (abre a Netflix, pausa...) e responde com [Reply.Voice]. */
    fun voice(text: String): String = JSONObject().put("type", "voice").put("text", text).toString()

    /** Liga (ou encerra) o microfone da TV: a fala é transcrita lá. */
    fun voiceListen(): String = JSONObject().put("type", "voice").put("action", "listen").toString()

    /** Resposta da TV, já interpretada. */
    sealed class Reply {
        data class Paired(val token: String, val tvName: String) : Reply()
        data class Ready(val tvName: String) : Reply()
        data class Volume(val value: Int?, val muted: Boolean) : Reply()
        /** A resposta da Emma a uma fala ([ok] falso: ela não conseguiu; [text] diz por quê ou o que fez). */
        data class Voice(val text: String, val ok: Boolean) : Reply()
        data class Error(val message: String, val unpaired: Boolean) : Reply()
        object Ok : Reply()
    }

    fun parse(text: String): Reply {
        val json = runCatching { JSONObject(text) }.getOrNull() ?: return Reply.Error("resposta inválida da TV", false)
        return when (json.optString("type")) {
            "paired" -> Reply.Paired(json.getString("token"), json.optString("name", "LUMNA"))
            "ready" -> Reply.Ready(json.optString("name", "LUMNA"))
            "volume" -> Reply.Volume(if (json.isNull("value")) null else json.optInt("value"), json.optBoolean("muted"))
            "voice_reply" -> Reply.Voice(json.optString("text", json.optString("error")), json.optBoolean("ok"))
            "error" -> Reply.Error(json.optString("message", "erro"), json.optBoolean("unpaired"))
            else -> Reply.Ok
        }
    }
}
