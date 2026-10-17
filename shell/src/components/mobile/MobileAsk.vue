<script setup lang="ts">
// Celular: pedidos que chegam do PC pelo Connect. Senha (um app do PC pediu permissão de
// administrador) ou confirmação de uma ação da Emma. A resposta volta pro PC.
import { nextTick, onMounted, onUnmounted, ref } from "vue";
import Icon from "../Icon.vue";
import { connectAskClose, connectSend, on, type ConnectAsk } from "../../system";

const asks = ref<ConnectAsk[]>([]);
const password = ref("");
const show = ref(false);
const busy = ref(false);
const error = ref("");
const input = ref<HTMLInputElement>();

const current = () => asks.value[0] ?? null;

function next() {
  asks.value = asks.value.slice(1);
  password.value = "";
  error.value = "";
  busy.value = false;
  if (!asks.value.length) connectAskClose();
}
async function sendPassword() {
  const a = current();
  if (!a || !password.value) return;
  busy.value = true;
  try {
    await connectSend(a.computer, { type: "auth-respond", cookie: a.cookie, password: password.value });
    // se a senha estiver errada o PC manda o pedido de novo (com o erro); senão, "auth-done"
    password.value = "";
  } catch (e) {
    error.value = String(e);
    busy.value = false;
  }
}
async function decide(allow: boolean) {
  const a = current();
  if (!a) return;
  busy.value = true;
  try {
    await connectSend(a.computer, { type: "emma-decide", call_id: a.call_id, allow });
    next();
  } catch (e) {
    error.value = String(e);
    busy.value = false;
  }
}
function dismiss() {
  next();
}

const stops: (() => void)[] = [];
onMounted(async () => {
  stops.push(
    await on<ConnectAsk>("lumna://connect-ask", async (ask) => {
      // o mesmo pedido de senha voltando (senha errada): atualiza no lugar
      const i = asks.value.findIndex((a) => a.kind === "auth" && a.cookie && a.cookie === ask.cookie);
      if (i >= 0) {
        asks.value[i] = ask;
        busy.value = false;
      } else asks.value = [...asks.value, ask];
      await nextTick();
      if (current()?.kind === "auth") input.value?.focus();
    }),
  );
  stops.push(
    await on<{ computer: string; cookie: string }>("lumna://connect-ask-done", ({ computer, cookie }) => {
      const first = current();
      asks.value = asks.value.filter((a) => !(a.computer === computer && a.cookie === cookie));
      if (first && first.cookie === cookie) {
        password.value = "";
        error.value = "";
        busy.value = false;
      }
      if (!asks.value.length) connectAskClose();
    }),
  );
});
onUnmounted(() => stops.forEach((s) => s()));
</script>

<template>
  <main class="veil">
    <div v-if="current()" class="card">
      <div class="from"><Icon name="desktop" :size="18" /> {{ current()!.computer_name }}</div>

      <template v-if="current()!.kind === 'auth'">
        <span class="badge"><Icon name="lock" :size="30" /></span>
        <h1>Senha de administrador</h1>
        <p class="msg">{{ current()!.message }}</p>
        <form class="pill" @submit.prevent="sendPassword()">
          <input
            ref="input"
            v-model="password"
            :type="show ? 'text' : 'password'"
            :placeholder="`Senha de ${current()!.user}`"
            autocomplete="off"
            :disabled="busy"
          />
          <button type="button" class="eye" aria-label="Mostrar senha" @click="show = !show"><Icon name="eye" :size="20" /></button>
        </form>
        <p v-if="current()!.error" class="err">{{ current()!.error }}</p>
        <p v-if="error" class="err">{{ error }}</p>
        <div class="row">
          <button class="btn" @click="dismiss()">Agora não</button>
          <button class="btn go" :disabled="!password || busy" @click="sendPassword()">{{ busy ? "Conferindo…" : "Autorizar" }}</button>
        </div>
      </template>

      <template v-else>
        <span class="badge emma"><Icon name="sparkle" :size="30" /></span>
        <h1>A Emma quer confirmar</h1>
        <p class="label">{{ current()!.label }}</p>
        <p v-if="current()!.reason" class="msg">{{ current()!.reason }}</p>
        <p v-if="error" class="err">{{ error }}</p>
        <div class="row">
          <button class="btn" :disabled="busy" @click="decide(false)">Não</button>
          <button class="btn go" :disabled="busy" @click="decide(true)">Pode fazer</button>
        </div>
      </template>
    </div>
  </main>
</template>

<style scoped>
.veil {
  height: 100vh;
  box-sizing: border-box;
  padding: 10vh 18px 0;
  display: flex;
  align-items: flex-start;
  justify-content: center;
  background: rgba(5, 3, 14, 0.75);
  user-select: none;
}
.card {
  width: 100%;
  padding: 20px 20px 22px;
  display: flex;
  flex-direction: column;
  align-items: center;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 26px;
  background: rgba(22, 18, 34, 0.97);
  text-align: center;
}
.from {
  display: flex;
  align-items: center;
  gap: 6px;
  color: var(--lumna-muted);
  font-size: 13px;
}
.badge {
  margin-top: 14px;
  width: 64px;
  height: 64px;
  display: grid;
  place-items: center;
  border-radius: 50%;
  background: rgba(139, 92, 246, 0.22);
  color: var(--lumna-violet-soft);
}
h1 {
  margin: 12px 0 6px;
  font-size: 20px;
  font-weight: 500;
}
.label {
  margin: 0 0 4px;
  font-size: 17px;
}
.msg {
  margin: 0 0 14px;
  color: var(--lumna-muted);
  font-size: 14px;
  line-height: 1.4;
}
.pill {
  width: 100%;
  display: flex;
  align-items: center;
  padding: 0 6px 0 18px;
  box-sizing: border-box;
  height: 56px;
  border: 1px solid rgba(255, 255, 255, 0.14);
  border-radius: 999px;
  background: rgba(10, 8, 18, 0.7);
}
.pill input {
  flex: 1;
  min-width: 0;
  border: 0;
  background: none;
  color: var(--lumna-text);
  font: inherit;
  font-size: 17px;
  outline: none;
}
.eye {
  width: 44px;
  height: 44px;
  display: grid;
  place-items: center;
  border: 0;
  background: none;
  color: var(--lumna-muted);
}
.err {
  margin: 10px 0 0;
  color: #fca5a5;
  font-size: 13px;
}
.row {
  width: 100%;
  display: flex;
  gap: 10px;
  margin-top: 18px;
}
.btn {
  flex: 1;
  padding: 14px 0;
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: 16px;
  background: none;
  color: inherit;
  font: inherit;
  font-size: 15px;
}
.btn.go {
  border: 0;
  background: #6d4fc4;
  color: #fff;
  font-weight: 600;
}
.btn:disabled {
  opacity: 0.45;
}
</style>
