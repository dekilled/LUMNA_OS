package os.lumna.remote

import android.content.Context

/** Tokens de pareamento (um por TV) e a última TV usada, no armazenamento privado do app. */
class Store(context: Context) {
    private val prefs = context.getSharedPreferences("lumna", Context.MODE_PRIVATE)

    fun token(tv: Tv): String? = prefs.getString("token:${key(tv)}", null)

    fun saveToken(tv: Tv, token: String) {
        prefs.edit().putString("token:${key(tv)}", token).putString("last", encode(tv)).apply()
    }

    fun forget(tv: Tv) {
        prefs.edit().remove("token:${key(tv)}").apply()
    }

    fun remember(tv: Tv) {
        prefs.edit().putString("last", encode(tv)).apply()
    }

    fun last(): Tv? = prefs.getString("last", null)?.let(::decode)

    /** TVs achadas pelo nome anunciado; as digitadas à mão, pelo endereço. */
    private fun key(tv: Tv) = if (tv.name.startsWith("LUMNA")) tv.name else "${tv.host}:${tv.port}"

    private fun encode(tv: Tv) = listOf(tv.name, tv.host, tv.port.toString()).joinToString("\u0001")

    private fun decode(text: String): Tv? {
        val parts = text.split("\u0001")
        return if (parts.size == 3) Tv(parts[0], parts[1], parts[2].toIntOrNull() ?: Protocol.PORT) else null
    }
}
