package os.lumna.remote

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ProtocolTest {
    @Test
    fun messagesMatchWhatTheTvExpects() {
        assertEquals("pad", JSONObject(Protocol.pad("up")).getString("type"))
        assertEquals("up", JSONObject(Protocol.pad("up")).getString("action"))
        val pair = JSONObject(Protocol.pair("123456", "Pixel"))
        assertEquals("123456", pair.getString("code"))
        assertEquals("Pixel", pair.getString("name"))
        assertEquals("play-pause", JSONObject(Protocol.media("play-pause")).getString("action"))
        assertEquals("home", JSONObject(Protocol.home()).getString("type"))
    }

    @Test
    fun textIsEscaped() {
        val value = "olá \"TV\"\n"
        assertEquals(value, JSONObject(Protocol.text(value)).getString("value"))
    }

    @Test
    fun readsTvReplies() {
        assertEquals(Protocol.Reply.Paired("abc", "LUMNA sala"), Protocol.parse("""{"type":"paired","token":"abc","name":"LUMNA sala"}"""))
        assertEquals(Protocol.Reply.Ready("LUMNA sala"), Protocol.parse("""{"type":"ready","name":"LUMNA sala"}"""))
        assertEquals(Protocol.Reply.Volume(40, false), Protocol.parse("""{"type":"volume","value":40,"muted":false}"""))
        assertEquals(Protocol.Reply.Volume(null, true), Protocol.parse("""{"type":"volume","value":null,"muted":true}"""))
        val error = Protocol.parse("""{"type":"error","message":"celular não pareado","unpaired":true}""")
        assertTrue(error is Protocol.Reply.Error && error.unpaired)
        assertEquals(Protocol.Reply.Ok, Protocol.parse("""{"type":"ok"}"""))
        assertTrue(Protocol.parse("lixo") is Protocol.Reply.Error)
    }
}
