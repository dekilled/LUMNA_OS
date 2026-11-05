package os.lumna.remote

import android.content.ContentValues
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import android.provider.OpenableColumns
import android.webkit.MimeTypeMap
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import java.io.File
import java.io.InputStream
import java.io.OutputStream
import java.util.UUID
import java.util.concurrent.Executors
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit

/**
 * Uma conexão só para uma transferência: entra com o token e vai pergunta e resposta, uma de cada vez
 * (o PC só lê o pedaço seguinte quando gravou o anterior, então um arquivo grande não enche a memória).
 * Fica separada da conexão do controle para os "ok" de um não se misturarem com os do outro.
 */
class Session(private val pc: Pc, private val token: String) {
    private val queue = LinkedBlockingQueue<Protocol.Reply>()
    private var socket: WebSocket? = null

    /** Abre e entra. Devolve o motivo da falha, ou null se entrou. */
    fun open(): String? {
        val request = Request.Builder().url("ws://${pc.host}:${pc.port}/").build()
        socket = PcLink.client.newWebSocket(request, object : WebSocketListener() {
            override fun onOpen(webSocket: WebSocket, response: Response) {
                webSocket.send(Protocol.hello(token))
            }

            override fun onMessage(webSocket: WebSocket, text: String) {
                queue.put(Protocol.parse(text))
            }

            override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
                queue.put(Protocol.Reply.Error("a conexão com o PC se encerrou", false))
            }

            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                queue.put(Protocol.Reply.Error("sem conexão com o PC", false))
            }
        })
        while (true) {
            val reply = queue.poll(10, TimeUnit.SECONDS) ?: return "o PC não respondeu"
            when (reply) {
                is Protocol.Reply.Ready -> return null
                is Protocol.Reply.Error -> return reply.message
                else -> {}
            }
        }
    }

    /** Manda e espera a resposta (um "ok", um erro ou um pedaço de arquivo). */
    fun request(message: String, waitSeconds: Long = 60): Protocol.Reply {
        if (socket?.send(message) != true) return Protocol.Reply.Error("a conexão com o PC caiu", false)
        while (true) {
            val reply = queue.poll(waitSeconds, TimeUnit.SECONDS) ?: return Protocol.Reply.Error("o PC não respondeu", false)
            when (reply) {
                is Protocol.Reply.Ok, is Protocol.Reply.Error, is Protocol.Reply.FileData -> return reply
                else -> {} // o que o PC avisa por conta própria (a área de transferência, um aviso) não é a resposta
            }
        }
    }

    fun close() {
        socket?.close(1000, "tchau")
        socket = null
    }
}

/** Enviar arquivos para o PC e receber os que o PC oferece. */
object Transfers {
    private val pool = Executors.newCachedThreadPool()

    private fun newId() = UUID.randomUUID().toString().replace("-", "").take(12)

    private fun begin(transfer: Connect.Transfer) {
        Connect.post {
            Connect.transfers.add(0, transfer)
            while (Connect.transfers.size > 20) Connect.transfers.removeAt(Connect.transfers.size - 1)
            Connect.emit(Connect.Event.Transfers)
        }
    }

    private fun progress(transfer: Connect.Transfer, done: Long) {
        Connect.post {
            transfer.done = done
            Connect.emit(Connect.Event.Transfers)
        }
    }

    private fun finish(transfer: Connect.Transfer, error: String?) {
        Connect.post {
            transfer.state = if (error == null) "done" else "error"
            transfer.error = error ?: ""
            if (error == null) transfer.done = transfer.size
            Connect.emit(Connect.Event.Transfers)
            if (error == null && !transfer.outgoing) Notifier.show(Connect.context, "LUMNA Connect", "Recebido: ${transfer.name}", "Salvo em Downloads/LUMNA", false)
        }
    }

    // ---- celular -> PC ----

    private fun describe(uri: Uri): Pair<String, Long> {
        val resolver = Connect.context.contentResolver
        var name = ""
        var size = -1L
        runCatching {
            resolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE), null, null, null)?.use { c ->
                if (c.moveToFirst()) {
                    name = c.getString(0) ?: ""
                    if (!c.isNull(1)) size = c.getLong(1)
                }
            }
        }
        if (size < 0) size = runCatching { resolver.openAssetFileDescriptor(uri, "r")?.use { it.length } }.getOrNull() ?: -1L
        if (name.isEmpty()) name = uri.lastPathSegment?.substringAfterLast('/') ?: "arquivo"
        return Pair(name, size)
    }

    fun upload(uri: Uri) {
        val target = Connect.pc
        val token = target?.let { Connect.store.token(it) }
        if (target == null || token == null) {
            Connect.toast("Conecte a um PC primeiro")
            return
        }
        if (!Connect.feature("files")) {
            Connect.toast("O envio de arquivos está desligado para este celular no PC")
            return
        }
        pool.execute {
            val (name, size) = describe(uri)
            val transfer = Connect.Transfer(newId(), name, maxOf(size, 0L), outgoing = true)
            begin(transfer)
            if (size < 0) return@execute finish(transfer, "não deu para saber o tamanho do arquivo")
            val session = Session(target, token)
            try {
                val failure = session.open()
                if (failure != null) return@execute finish(transfer, failure)
                val input: InputStream = Connect.context.contentResolver.openInputStream(uri)
                    ?: return@execute finish(transfer, "não consegui abrir o arquivo")
                input.use { stream ->
                    var reply = session.request(Protocol.fileBegin(transfer.id, name, size), 30)
                    if (reply is Protocol.Reply.Error) return@execute finish(transfer, reply.message)
                    val buffer = ByteArray(Protocol.CHUNK)
                    var sent = 0L
                    while (true) {
                        val n = stream.read(buffer)
                        if (n < 0) break
                        if (n == 0) continue
                        reply = session.request(Protocol.fileChunk(transfer.id, buffer, n))
                        if (reply is Protocol.Reply.Error) return@execute finish(transfer, reply.message)
                        sent += n
                        progress(transfer, sent)
                    }
                    reply = session.request(Protocol.fileEnd(transfer.id), 60)
                    if (reply is Protocol.Reply.Error) return@execute finish(transfer, reply.message)
                    finish(transfer, null)
                }
            } catch (e: Exception) {
                session.request(Protocol.fileCancel(transfer.id), 5)
                finish(transfer, e.message ?: "erro ao enviar")
            } finally {
                session.close()
            }
        }
    }

    // ---- PC -> celular ----

    /** O PC ofereceu um arquivo: puxa pedaço por pedaço e guarda em Downloads/LUMNA. */
    fun download(offer: Protocol.Reply.FileOffer) {
        val target = Connect.pc
        val token = target?.let { Connect.store.token(it) }
        if (target == null || token == null) return
        pool.execute {
            val transfer = Connect.Transfer(offer.id, offer.name, offer.size, outgoing = false)
            begin(transfer)
            val session = Session(target, token)
            var sink: Sink? = null
            try {
                val failure = session.open()
                if (failure != null) return@execute finish(transfer, failure)
                sink = Sink.create(offer.name)
                var offset = 0L
                while (true) {
                    val reply = session.request(Protocol.filePull(offer.id, offset))
                    if (reply is Protocol.Reply.Error) {
                        sink.abort()
                        return@execute finish(transfer, reply.message)
                    }
                    val data = reply as? Protocol.Reply.FileData
                    if (data == null) {
                        sink.abort()
                        return@execute finish(transfer, "resposta inesperada do PC")
                    }
                    sink.out.write(data.bytes)
                    offset += data.bytes.size
                    progress(transfer, offset)
                    if (data.eof) break
                }
                sink.finish()
                finish(transfer, null)
            } catch (e: Exception) {
                sink?.abort()
                session.request(Protocol.fileReject(offer.id, e.message ?: "erro ao receber"), 5)
                finish(transfer, e.message ?: "erro ao receber")
            } finally {
                session.close()
            }
        }
    }

    /** Onde o arquivo recebido é gravado: a pasta Downloads/LUMNA (pelo MediaStore, sem pedir permissão). */
    class Sink private constructor(val out: OutputStream, private val uri: Uri?, private val file: File?) {
        fun finish() {
            out.close()
            if (uri != null) {
                val values = ContentValues().apply { put(MediaStore.MediaColumns.IS_PENDING, 0) }
                Connect.context.contentResolver.update(uri, values, null, null)
            }
        }

        fun abort() {
            runCatching { out.close() }
            uri?.let { u -> runCatching { Connect.context.contentResolver.delete(u, null, null) } }
            file?.delete()
        }

        companion object {
            fun create(name: String): Sink {
                val safe = name.replace(Regex("[/\\\\:*?\"<>|\\u0000-\\u001f]"), "_").trim().ifEmpty { "arquivo" }
                val context = Connect.context
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    val mime = MimeTypeMap.getSingleton().getMimeTypeFromExtension(safe.substringAfterLast('.', "").lowercase())
                    val values = ContentValues().apply {
                        put(MediaStore.MediaColumns.DISPLAY_NAME, safe)
                        put(MediaStore.MediaColumns.MIME_TYPE, mime ?: "application/octet-stream")
                        put(MediaStore.MediaColumns.RELATIVE_PATH, "${Environment.DIRECTORY_DOWNLOADS}/LUMNA")
                        put(MediaStore.MediaColumns.IS_PENDING, 1)
                    }
                    val uri = context.contentResolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values)
                        ?: throw IllegalStateException("não consegui criar o arquivo em Downloads")
                    val out = context.contentResolver.openOutputStream(uri) ?: throw IllegalStateException("não consegui gravar em Downloads")
                    return Sink(out, uri, null)
                }
                // Android 8 e 9: a pasta do próprio app (a de Downloads pediria permissão de armazenamento).
                val dir = File(context.getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS), "LUMNA").apply { mkdirs() }
                var file = File(dir, safe)
                var n = 1
                while (file.exists()) file = File(dir, "${safe.substringBeforeLast('.')} ($n).${safe.substringAfterLast('.', "")}".trimEnd('.')).also { n++ }
                return Sink(file.outputStream(), null, file)
            }
        }
    }
}
