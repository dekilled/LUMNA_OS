package os.lumna.remote

import android.content.Context

/** Tokens de pareamento (um por PC), o último PC usado e os ajustes do app, no armazenamento privado. */
class Store(context: Context) {
    private val prefs = context.getSharedPreferences("lumna", Context.MODE_PRIVATE)

    fun token(pc: Pc): String? = prefs.getString("token:${key(pc)}", null)

    fun saveToken(pc: Pc, token: String) {
        prefs.edit().putString("token:${key(pc)}", token).putString("last", encode(pc)).apply()
    }

    fun forget(pc: Pc) {
        prefs.edit().remove("token:${key(pc)}").apply()
    }

    fun remember(pc: Pc) {
        prefs.edit().putString("last", encode(pc)).apply()
    }

    fun last(): Pc? = prefs.getString("last", null)?.let(::decode)

    /** Mantém a conexão com o PC mesmo com o app fechado (um serviço com aviso fixo). */
    var keepAlive: Boolean
        get() = prefs.getBoolean("keepAlive", false)
        set(value) = prefs.edit().putBoolean("keepAlive", value).apply()

    /** O que o celular copiar vai sozinho para o PC (só com o app aberto: o Android não deixa ler a cópia em segundo plano). */
    var autoClipboard: Boolean
        get() = prefs.getBoolean("autoClipboard", false)
        set(value) = prefs.edit().putBoolean("autoClipboard", value).apply()

    /** Os avisos do celular aparecem no PC (precisa da permissão de acesso às notificações). */
    var mirrorPhone: Boolean
        get() = prefs.getBoolean("mirrorPhone", true)
        set(value) = prefs.edit().putBoolean("mirrorPhone", value).apply()

    /** PCs achados pelo nome anunciado; os digitados à mão, pelo endereço. */
    private fun key(pc: Pc) = if (pc.name.startsWith("LUMNA")) pc.name else "${pc.host}:${pc.port}"

    private fun encode(pc: Pc) = listOf(pc.name, pc.host, pc.port.toString()).joinToString("\u0001")

    private fun decode(text: String): Pc? {
        val parts = text.split("\u0001")
        return if (parts.size == 3) Pc(parts[0], parts[1], parts[2].toIntOrNull() ?: Protocol.PORT) else null
    }
}
