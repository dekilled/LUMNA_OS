package os.lumna.remote

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import java.util.concurrent.CopyOnWriteArrayList

/**
 * A conexão do app com o PC, uma só para todas as telas: o estado (informações, agendamentos, conversa com
 * a Emma, área de transferência, transferências de arquivo) mora aqui e as telas só mostram. Tudo roda na
 * thread principal; o trabalho de rede e de arquivo fica nas threads de [Transfers] e posta de volta.
 */
object Connect : PcLink.Listener {
    sealed class Event {
        object Status : Event()
        object Info : Event()
        object Schedule : Event()
        object Chat : Event()
        object Clips : Event()
        object Transfers : Event()
        object Features : Event()
        data class Toast(val text: String) : Event()
        /** O PC não conhece mais este celular (ou o código estava errado): pedir o código de novo. */
        data class NeedCode(val pc: Pc) : Event()
    }

    interface Listener {
        fun onEvent(event: Event)
    }

    data class ChatLine(val fromMe: Boolean, val text: String, val error: Boolean = false)

    class Transfer(val id: String, val name: String, val size: Long, val outgoing: Boolean) {
        var done = 0L
        /** running | done | error */
        var state = "running"
        var error = ""
    }

    private lateinit var appContext: Context
    lateinit var store: Store
        private set

    private val main = Handler(Looper.getMainLooper())
    private val listeners = CopyOnWriteArrayList<Listener>()
    private var link: PcLink? = null
    private var pairing = false
    private var retries = 0
    /** Abriu a conexão e ainda não entrou. */
    private var connecting = false

    var pc: Pc? = null
        private set
    var connected = false
        private set
    var status = "Sem PC"
        private set
    var features: Map<String, Boolean> = emptyMap()
        private set
    var info: Protocol.Reply.Info? = null
        private set
    var tasks: List<Protocol.Task> = emptyList()
        private set
    val chat = mutableListOf<ChatLine>()
    var emmaBusy = false
        private set
    /** O que o PC copiou (o mais novo primeiro). */
    val clips = mutableListOf<String>()
    val transfers = mutableListOf<Transfer>()

    /** O serviço que mantém a conexão com o app fechado está ligado. */
    var keepAlive = false

    /** Quer estar conectado (falso depois de "Trocar PC"): decide se a conexão se refaz sozinha. */
    private var wanted = false
    private var lastClip: String? = null

    fun init(context: Context) {
        if (!::appContext.isInitialized) {
            appContext = context.applicationContext
            store = Store(appContext)
        }
    }

    val context: Context get() = appContext

    fun addListener(l: Listener) {
        listeners.addIfAbsent(l)
    }

    fun removeListener(l: Listener) {
        listeners.remove(l)
    }

    fun emit(event: Event) {
        listeners.forEach { it.onEvent(event) }
    }

    /** Roda na thread principal (as transferências chamam daqui de outras threads). */
    fun post(block: () -> Unit) {
        main.post(block)
    }

    fun toast(text: String) = emit(Event.Toast(text))

    fun feature(name: String): Boolean = features[name] != false

    // ---- conexão ----

    fun deviceName() = "${Build.MANUFACTURER.replaceFirstChar(Char::uppercase)} ${Build.MODEL}".take(40)

    fun connect(target: Pc) {
        val token = store.token(target)
        if (token == null) {
            emit(Event.NeedCode(target))
            return
        }
        open(target, Protocol.hello(token))
    }

    fun pair(target: Pc, code: String) {
        open(target, Protocol.pair(code, deviceName()))
        pairing = true
    }

    /** Volta ao último PC (depois de o sistema matar o app, por exemplo). */
    fun resume() {
        if (connected || connecting) return
        val target = pc ?: store.last() ?: return
        if (store.token(target) == null) return
        retries = 0
        connect(target)
    }

    private fun open(target: Pc, first: String) {
        main.removeCallbacks(reconnect)
        link?.close()
        if (pc != target) {
            // outro PC: o que era do anterior não vale
            info = null
            tasks = emptyList()
            features = emptyMap()
        }
        pc = target
        pairing = false
        wanted = true
        connected = false
        connecting = true
        status = "Conectando…"
        emit(Event.Status)
        link = PcLink(target.host, target.port, this).also { it.open(first) }
    }

    fun disconnect() {
        wanted = false
        main.removeCallbacks(reconnect)
        link?.close()
        link = null
        connected = false
        connecting = false
        pc = null
        status = "Sem PC"
        emit(Event.Status)
    }

    private val reconnect = Runnable {
        val target = pc
        if (wanted && target != null) connect(target)
    }

    private fun scheduleReconnect() {
        if (!wanted || pc == null) return
        // Sem o serviço fixo, desiste depois de algumas tentativas (o app volta a tentar quando abrir).
        if (!keepAlive && retries >= 5) return
        retries++
        main.removeCallbacks(reconnect)
        main.postDelayed(reconnect, minOf(3000L * retries, 30000L))
    }

    fun send(message: String): Boolean {
        if (link?.send(message) == true) return true
        if (wanted && !connected) {
            main.removeCallbacks(reconnect)
            main.post(reconnect)
        }
        return false
    }

    override fun onConnected() {}

    override fun onClosed(reason: String) {
        connected = false
        connecting = false
        status = "Desconectado: $reason"
        emit(Event.Status)
        scheduleReconnect()
    }

    override fun onReply(reply: Protocol.Reply) {
        val current = pc ?: return
        when (reply) {
            is Protocol.Reply.Paired -> {
                store.saveToken(current, reply.token)
                pairing = false
                entered("Pareado com ${reply.pcName}")
            }
            is Protocol.Reply.Ready -> {
                store.remember(current)
                entered(reply.pcName)
            }
            is Protocol.Reply.Volume -> {
                status = if (reply.muted) "Som desligado" else "Volume ${reply.value ?: "-"}%"
                emit(Event.Status)
            }
            is Protocol.Reply.Error -> {
                if (reply.unpaired || pairing) {
                    // Token velho (o PC esqueceu este celular) ou código errado: pede o código de novo.
                    store.forget(current)
                    wanted = false
                    link?.close()
                    link = null
                    connected = false
                    connecting = false
                    status = "Precisa parear de novo"
                    emit(Event.Status)
                    toast(reply.message)
                    emit(Event.NeedCode(current))
                } else if (emmaBusy) {
                    emmaBusy = false
                    chat.add(ChatLine(false, reply.message, error = true))
                    emit(Event.Chat)
                } else {
                    toast(reply.message)
                }
            }
            is Protocol.Reply.Voice -> {
                emmaBusy = false
                chat.add(ChatLine(false, reply.text, error = !reply.ok))
                emit(Event.Chat)
                if (!reply.ok) toast(reply.text)
            }
            is Protocol.Reply.EmmaReply -> {
                emmaBusy = false
                chat.add(ChatLine(false, reply.content))
                emit(Event.Chat)
            }
            is Protocol.Reply.Info -> {
                info = reply
                emit(Event.Info)
            }
            is Protocol.Reply.Schedule -> {
                tasks = reply.tasks
                emit(Event.Schedule)
            }
            is Protocol.Reply.Features -> {
                features = reply.on
                emit(Event.Features)
                if (feature("control")) send(Protocol.scheduleList())
            }
            is Protocol.Reply.Clipboard -> receivedClipboard(reply.text)
            is Protocol.Reply.Notification -> Notifier.show(appContext, reply.app, reply.summary, reply.body, reply.urgent)
            is Protocol.Reply.FileOffer -> {
                if (feature("files")) Transfers.download(reply) else send(Protocol.fileReject(reply.id, "o celular não aceita arquivos"))
            }
            else -> {}
        }
    }

    private fun entered(name: String) {
        connected = true
        connecting = false
        retries = 0
        status = name
        emit(Event.Status)
        send(Protocol.features())
        send(Protocol.info())
    }

    /** Pede de novo o que as telas do PC mostram. */
    fun refreshPc() {
        send(Protocol.info())
        if (feature("control")) send(Protocol.scheduleList())
    }

    // ---- Emma ----

    fun askEmma(text: String) {
        chat.add(ChatLine(true, text))
        emmaBusy = true
        emit(Event.Chat)
        if (!send(Protocol.emma(text))) {
            emmaBusy = false
            chat.add(ChatLine(false, "Sem conexão com o PC.", error = true))
            emit(Event.Chat)
        }
    }

    /** O que o reconhecimento de voz do celular entendeu: a Emma do PC age e responde. */
    fun say(text: String) {
        chat.add(ChatLine(true, text))
        emmaBusy = true
        emit(Event.Chat)
        if (!send(Protocol.voice(text))) {
            emmaBusy = false
            chat.add(ChatLine(false, "Sem conexão com o PC.", error = true))
            emit(Event.Chat)
        }
    }

    fun newChat() {
        chat.clear()
        emmaBusy = false
        emit(Event.Chat)
    }

    // ---- área de transferência ----

    private fun clipboardManager() = appContext.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager

    private fun receivedClipboard(text: String) {
        if (text.isEmpty() || !feature("clipboard")) return
        lastClip = text
        runCatching { clipboardManager().setPrimaryClip(ClipData.newPlainText("LUMNA", text)) }
        clips.remove(text)
        clips.add(0, text)
        while (clips.size > 10) clips.removeAt(clips.size - 1)
        emit(Event.Clips)
    }

    fun copyLocally(text: String) {
        lastClip = text
        runCatching { clipboardManager().setPrimaryClip(ClipData.newPlainText("LUMNA", text)) }
    }

    /** Manda o que está copiado no celular para o PC. Só funciona com o app na frente (regra do Android). */
    fun sendClipboard(quiet: Boolean = false) {
        val text = runCatching {
            clipboardManager().primaryClip?.takeIf { it.itemCount > 0 }?.getItemAt(0)?.coerceToText(appContext)?.toString()
        }.getOrNull()
        if (text.isNullOrEmpty()) {
            if (!quiet) toast("Não há texto copiado")
            return
        }
        if (text == lastClip) {
            if (!quiet) toast("O PC já tem esse texto")
            return
        }
        if (!feature("clipboard")) {
            if (!quiet) toast("A área de transferência está desligada para este celular no PC")
            return
        }
        if (send(Protocol.clipboard(text))) {
            lastClip = text
            if (!quiet) toast("Enviado para o PC")
        } else if (!quiet) {
            toast("Sem conexão com o PC")
        }
    }

    /** Um texto compartilhado de outro app: vai para a área de transferência do PC. */
    fun sendSharedText(text: String) {
        if (send(Protocol.clipboard(text))) {
            lastClip = text
            toast("Texto enviado para a área de transferência do PC")
        } else {
            toast("Sem conexão com o PC")
        }
    }

    // ---- avisos do celular ----

    fun sendPhoneNotification(app: String, summary: String, body: String, urgent: Boolean) {
        if (!connected || !feature("phone_notifications") || !store.mirrorPhone) return
        send(Protocol.notify(app, summary, body, urgent))
    }
}
