package os.lumna.remote

import android.os.Handler
import android.os.Looper
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import java.util.concurrent.TimeUnit

/**
 * Conexão com uma TV. Ao abrir, entra com o token salvo (ou pareia com o código digitado);
 * depois só manda comandos. As respostas chegam na thread principal.
 */
class TvLink(
    private val host: String,
    private val port: Int,
    private val listener: Listener,
) {
    interface Listener {
        fun onConnected()
        fun onReply(reply: Protocol.Reply)
        fun onClosed(reason: String)
    }

    private val main = Handler(Looper.getMainLooper())
    private var socket: WebSocket? = null

    /** Abre a conexão e manda [first] (o `hello` com o token, ou o `pair` com o código). */
    fun open(first: String) {
        val request = Request.Builder().url("ws://$host:$port/").build()
        socket = client.newWebSocket(request, object : WebSocketListener() {
            override fun onOpen(webSocket: WebSocket, response: Response) {
                webSocket.send(first)
                main.post { listener.onConnected() }
            }

            override fun onMessage(webSocket: WebSocket, text: String) {
                val reply = Protocol.parse(text)
                main.post { listener.onReply(reply) }
            }

            override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
                main.post { listener.onClosed(reason.ifEmpty { "conexão encerrada" }) }
            }

            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                main.post { listener.onClosed("sem conexão com a TV (${t.message ?: t.javaClass.simpleName})") }
            }
        })
    }

    fun send(message: String): Boolean = socket?.send(message) ?: false

    fun close() {
        socket?.close(1000, "tchau")
        socket = null
    }

    companion object {
        private val client = OkHttpClient.Builder()
            .connectTimeout(5, TimeUnit.SECONDS)
            // Mantém a conexão viva quando o Wi-Fi do celular economiza energia.
            .pingInterval(20, TimeUnit.SECONDS)
            .build()
    }
}
