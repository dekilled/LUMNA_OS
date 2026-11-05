package os.lumna.remote

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ProtocolTest {
    @Test
    fun messagesMatchWhatThePcExpects() {
        assertEquals("pad", JSONObject(Protocol.pad("up")).getString("type"))
        assertEquals("up", JSONObject(Protocol.pad("up")).getString("action"))
        val pair = JSONObject(Protocol.pair("123456", "Pixel"))
        assertEquals("123456", pair.getString("code"))
        assertEquals("Pixel", pair.getString("name"))
        assertEquals("play-pause", JSONObject(Protocol.media("play-pause")).getString("action"))
        assertEquals("home", JSONObject(Protocol.home()).getString("type"))
    }

    @Test
    fun voiceGoesToTheEmma() {
        val said = JSONObject(Protocol.voice("abre a \"Netflix\""))
        assertEquals("voice", said.getString("type"))
        assertEquals("abre a \"Netflix\"", said.getString("text"))
        assertEquals("listen", JSONObject(Protocol.voiceListen()).getString("action"))
        assertEquals(Protocol.Reply.Voice("Abrindo Netflix.", true), Protocol.parse("""{"type":"voice_reply","ok":true,"local":true,"text":"Abrindo Netflix."}"""))
        assertEquals(Protocol.Reply.Voice("a Emma não está rodando", false), Protocol.parse("""{"type":"voice_reply","ok":false,"error":"a Emma não está rodando"}"""))
    }

    @Test
    fun textIsEscaped() {
        val value = "olá \"TV\"\n"
        assertEquals(value, JSONObject(Protocol.text(value)).getString("value"))
    }

    @Test
    fun readsPcReplies() {
        assertEquals(Protocol.Reply.Paired("abc", "LUMNA sala"), Protocol.parse("""{"type":"paired","token":"abc","name":"LUMNA sala"}"""))
        assertEquals(Protocol.Reply.Ready("LUMNA sala"), Protocol.parse("""{"type":"ready","name":"LUMNA sala"}"""))
        assertEquals(Protocol.Reply.Volume(40, false), Protocol.parse("""{"type":"volume","value":40,"muted":false}"""))
        assertEquals(Protocol.Reply.Volume(null, true), Protocol.parse("""{"type":"volume","value":null,"muted":true}"""))
        val error = Protocol.parse("""{"type":"error","message":"celular não pareado","unpaired":true}""")
        assertTrue(error is Protocol.Reply.Error && error.unpaired)
        assertEquals(Protocol.Reply.Ok, Protocol.parse("""{"type":"ok"}"""))
        assertTrue(Protocol.parse("lixo") is Protocol.Reply.Error)
    }

    @Test
    fun remoteControlMessages() {
        assertEquals("poweroff", JSONObject(Protocol.power("poweroff")).getString("action"))
        assertEquals("info", JSONObject(Protocol.info()).getString("type"))
        val add = JSONObject(Protocol.scheduleAdd("remind", 1800000000L, "tirar o pão", true))
        assertEquals("schedule-add", add.getString("type"))
        assertEquals(1800000000L, add.getJSONObject("task").getLong("at"))
        assertEquals("tirar o pão", add.getJSONObject("task").getString("text"))
        assertTrue(add.getJSONObject("task").getBoolean("daily"))
        assertEquals("abc", JSONObject(Protocol.scheduleRemove("abc")).getString("id"))
        assertEquals("oi", JSONObject(Protocol.emma("oi")).getString("text"))
    }

    @Test
    fun readsInfoScheduleAndFeatures() {
        val info = Protocol.parse("""{"type":"info","host":"sala","user":"eric","uptime":5400,"load":"0.42","memory_total_kb":16000000,"memory_available_kb":9000000,"battery":77,"charging":true}""")
        assertEquals(Protocol.Reply.Info("sala", "eric", 5400, "0.42", 16000000, 9000000, 77, true), info)
        val noBattery = Protocol.parse("""{"type":"info","host":"a","user":"b","uptime":1,"load":null,"memory_total_kb":1,"memory_available_kb":1,"battery":null,"charging":false}""")
        assertTrue(noBattery is Protocol.Reply.Info && noBattery.battery == null && noBattery.load == null)
        val schedule = Protocol.parse("""{"type":"schedule","tasks":[{"id":"a1","at":1800000000,"action":"remind","text":"x","daily":false,"by":"Celular"}]}""")
        assertEquals(Protocol.Reply.Schedule(listOf(Protocol.Task("a1", 1800000000L, "remind", "x", false, "Celular"))), schedule)
        assertEquals(Protocol.Reply.Schedule(emptyList()), Protocol.parse("""{"type":"schedule","tasks":[]}"""))
        val features = Protocol.parse("""{"type":"features","features":{"clipboard":true,"files":false}}""")
        assertEquals(Protocol.Reply.Features(mapOf("clipboard" to true, "files" to false)), features)
        assertEquals(Protocol.Reply.EmmaReply("pronto"), Protocol.parse("""{"type":"emma-reply","content":"pronto"}"""))
    }

    @Test
    fun filesTravelInChunks() {
        val bytes = ByteArray(10) { it.toByte() }
        val chunk = JSONObject(Protocol.fileChunk("t1", bytes, 4))
        assertEquals("file-chunk", chunk.getString("type"))
        assertEquals(java.util.Base64.getEncoder().encodeToString(byteArrayOf(0, 1, 2, 3)), chunk.getString("data"))
        val begin = JSONObject(Protocol.fileBegin("t1", "foto.jpg", 123456789012L))
        assertEquals(123456789012L, begin.getLong("size"))
        assertEquals(Protocol.Reply.FileOffer("o1", "a.bin", 99), Protocol.parse("""{"type":"file-offer","id":"o1","name":"a.bin","size":99}"""))
        val data = Protocol.parse("""{"type":"file-data","data":"AAEC","len":3,"eof":true}""")
        assertTrue(data is Protocol.Reply.FileData && data.eof && data.bytes.contentEquals(byteArrayOf(0, 1, 2)))
        assertEquals(12, JSONObject(Protocol.filePull("o1", 12)).getLong("offset").toInt())
        assertTrue(Protocol.parse("""{"type":"file-data","data":"!!!"}""") is Protocol.Reply.Error)
    }

    @Test
    fun readsNotificationsAndClipboardFromThePc() {
        assertEquals(
            Protocol.Reply.Notification("Agendamento", "Lembrete", "tirar o pão", true),
            Protocol.parse("""{"type":"notification","app":"Agendamento","summary":"Lembrete","body":"tirar o pão","urgent":true}"""),
        )
        assertEquals(Protocol.Reply.Clipboard("copiado"), Protocol.parse("""{"type":"clipboard","text":"copiado"}"""))
        val sent = JSONObject(Protocol.notify("WhatsApp", "Mãe", "oi", false))
        assertEquals("notify", sent.getString("type"))
        assertEquals("WhatsApp", sent.getString("app"))
        assertEquals(Protocol.Reply.Other("mode"), Protocol.parse("""{"type":"mode","media":null}"""))
    }
}
