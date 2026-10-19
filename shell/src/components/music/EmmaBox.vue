<script setup lang="ts">
// A Emma no LUMNA Music (PC): "toca algo calmo", "quais álbuns do Coldplay eu tenho?". Vai pela
// conversa do assistente, com o contexto do app (ela toca pela ferramenta music.play).
import { computed, onMounted, onUnmounted, ref } from "vue";
import Icon from "../Icon.vue";
import { emmaListen, emmaListenStop, emmaSend, on, type EmmaEvent, type EmmaVoice } from "../../system";
import { parseReply, plainText } from "../emma/assist";

const props = defineProps<{ context: string }>();
const draft = ref("");
const answer = ref("");
const asked = ref("");
const busy = ref(false);
const mic = ref<"idle" | "listening" | "transcribing">("idle");
let mine = false;
const reply = computed(() => parseReply(answer.value));

async function send(text = draft.value) {
  text = text.trim();
  if (!text || busy.value) return;
  draft.value = "";
  asked.value = text;
  answer.value = "";
  busy.value = true;
  try {
    await emmaSend(text, false, "assist", props.context);
  } catch (e) {
    answer.value = String(e);
  } finally {
    busy.value = false;
  }
}
async function toggleMic() {
  if (mic.value !== "idle") return emmaListenStop(false).catch(() => {});
  mic.value = "listening";
  mine = true;
  try {
    await emmaListen();
  } catch (e) {
    mic.value = "idle";
    mine = false;
    answer.value = String(e);
  }
}

const stops: (() => void)[] = [];
onMounted(async () => {
  stops.push(
    await on<EmmaEvent>("lumna://emma", (e) => {
      if (e.channel !== "assist" || !asked.value) return;
      if (e.type === "delta") answer.value += e.content;
      else if (e.type === "rewrite") answer.value = e.content;
      else if (e.type === "done" && e.message?.content) answer.value = e.message.content;
      else if (e.type === "error") answer.value = e.message;
    }),
  );
  stops.push(
    await on<EmmaVoice>("lumna://emma-voice", (v) => {
      if (!mine) return;
      if (v.state === "transcribing") mic.value = "transcribing";
      else if (v.state === "heard") {
        mic.value = "idle";
        mine = false;
        send(v.text);
      } else if (v.state === "idle" || v.state === "error") {
        mic.value = "idle";
        mine = false;
      }
    }),
  );
});
onUnmounted(() => stops.forEach((s) => s()));
</script>

<template>
  <section class="emma-box">
    <header>
      <svg class="gem" viewBox="0 0 24 24" aria-hidden="true"><path d="M12 1.5 21.5 12 12 22.5 2.5 12z" fill="#8b5cf6" /></svg>
      <strong>Emma</strong>
    </header>
    <p v-if="!asked" class="hint">Encontre uma música na biblioteca</p>
    <div v-else class="talk">
      <p class="you">{{ asked }}</p>
      <p v-if="reply.body" class="answer">{{ plainText(reply.body) }}</p>
      <p v-else-if="busy" class="answer typing"><span /><span /><span /></p>
      <div v-if="reply.suggestions.length || reply.primary" class="chips">
        <button v-for="s in [reply.primary, ...reply.suggestions].filter(Boolean)" :key="s!" @click="send(s!)">{{ s }}</button>
      </div>
    </div>
    <form class="field" :class="{ on: mic !== 'idle' }" @submit.prevent="send()">
      <button type="button" class="mic" :aria-label="mic === 'idle' ? 'Falar' : 'Parar'" @click="toggleMic()"><Icon name="mic" :size="22" /></button>
      <input v-model="draft" :placeholder="mic === 'listening' ? 'Ouvindo…' : mic === 'transcribing' ? 'Entendendo…' : 'Pergunte ou digite...'" :disabled="mic !== 'idle'" />
    </form>
  </section>
</template>

<style scoped>
.emma-box {
  display: flex;
  flex-direction: column;
  gap: 10px;
  padding-top: 18px;
  border-top: 1px solid rgba(167, 139, 250, 0.14);
}
header {
  display: flex;
  align-items: center;
  gap: 12px;
}
.gem {
  width: 26px;
  height: 26px;
  filter: drop-shadow(0 0 8px rgba(139, 92, 246, 0.6));
}
header strong {
  font-size: 18px;
}
.hint {
  margin: 0;
  color: #d9d3f0;
  font-size: 14px;
}
.talk {
  max-height: 180px;
  overflow-y: auto;
  display: flex;
  flex-direction: column;
  gap: 6px;
  font-size: 14px;
}
.you {
  align-self: flex-end;
  margin: 0;
  padding: 6px 12px;
  border-radius: 14px;
  background: #241e35;
}
.answer {
  margin: 0;
  line-height: 1.5;
  color: #e7e2fb;
}
.typing span {
  display: inline-block;
  width: 6px;
  height: 6px;
  margin-right: 4px;
  border-radius: 50%;
  background: var(--lumna-violet-soft);
  animation: blink 1s infinite alternate;
}
.typing span:nth-child(2) {
  animation-delay: 0.2s;
}
.typing span:nth-child(3) {
  animation-delay: 0.4s;
}
@keyframes blink {
  from {
    opacity: 0.25;
  }
}
.chips {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}
.chips button {
  padding: 6px 10px;
  border: 1px solid rgba(167, 139, 250, 0.3);
  border-radius: 999px;
  background: none;
  color: var(--lumna-violet-soft);
  font: inherit;
  font-size: 13px;
}
.field {
  display: flex;
  align-items: center;
  gap: 8px;
  height: 62px;
  padding: 0 14px 0 8px;
  border: 1.5px solid rgba(139, 92, 246, 0.75);
  border-radius: 14px;
  background: rgba(20, 16, 34, 0.7);
}
.field.on {
  box-shadow: 0 0 0 3px rgba(139, 92, 246, 0.25);
}
.mic {
  width: 44px;
  height: 44px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: none;
  color: var(--lumna-violet-soft);
}
.field input {
  flex: 1;
  min-width: 0;
  border: 0;
  background: none;
  color: inherit;
  font: inherit;
  font-size: 15px;
  outline: none;
}
.field input::placeholder {
  color: var(--lumna-muted);
}
</style>
