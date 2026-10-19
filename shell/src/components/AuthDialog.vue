<script setup lang="ts">
import { nextTick, onMounted, onUnmounted, ref } from "vue";
import Icon from "./Icon.vue";
import { authRespond, on, type AuthRequest } from "../system";

// Tela de senha de administrador: aparece quando algo pede permissão pelo polkit.
const request = ref<AuthRequest | null>(null);
const password = ref("");
const reveal = ref(false);
const checking = ref(false);
const input = ref<HTMLInputElement>();
const shake = ref(false);
// celular: o LUMNA Keyboard fica por cima da camada; o cartão sobe acima dele
const inset = ref(0);

async function show(r: AuthRequest) {
  const retry = request.value?.cookie === r.cookie && r.error;
  request.value = r;
  password.value = "";
  checking.value = false;
  if (retry) {
    shake.value = false;
    await nextTick();
    shake.value = true;
  }
  await nextTick();
  input.value?.focus();
}

function submit() {
  if (!request.value || !password.value || checking.value) return;
  checking.value = true;
  authRespond(request.value.cookie, password.value);
  password.value = "";
}

function cancel() {
  if (request.value) authRespond(request.value.cookie, null);
  request.value = null;
}

function onKey(event: KeyboardEvent) {
  if (event.key === "Escape" && request.value) cancel();
}

const stops: (() => void)[] = [];

onMounted(async () => {
  window.addEventListener("keydown", onKey);
  stops.push(() => window.removeEventListener("keydown", onKey));
  stops.push(await on<AuthRequest>("lumna://auth-request", show));
  stops.push(
    await on<{ shown: boolean; lumna: boolean; height: number }>("lumna://keyboard", (k) => {
      inset.value = k?.shown && k.lumna ? (k.height ?? 404) : 0;
    }),
  );
  stops.push(
    await on("lumna://auth-done", () => {
      request.value = null;
      checking.value = false;
    }),
  );
});

onUnmounted(() => stops.forEach((stop) => stop()));
</script>

<template>
  <div class="veil" :class="{ shown: request }" :style="{ paddingBottom: `${inset}px` }">
    <form v-if="request" class="card" :class="{ shake }" @submit.prevent="submit()" @animationend="shake = false">
      <div class="lock"><Icon name="lock" :size="26" /></div>
      <h1>Permissão de administrador</h1>
      <p class="message">{{ request.message }}</p>

      <label class="field" :class="{ wrong: request.error && !checking }">
        <span class="user">{{ request.user }}</span>
        <input
          ref="input"
          v-model="password"
          :type="reveal ? 'text' : 'password'"
          placeholder="Senha"
          autocomplete="off"
          spellcheck="false"
          :disabled="checking"
          @keydown.enter.prevent="submit()"
        />
        <button type="button" class="eye" :title="reveal ? 'Esconder senha' : 'Mostrar senha'" @click="reveal = !reveal">
          <Icon name="eye" :size="16" />
        </button>
      </label>
      <p class="error">{{ checking ? "Conferindo…" : request.error ?? "" }}</p>

      <div class="buttons">
        <button type="button" class="btn" @click="cancel()">Cancelar</button>
        <button type="submit" class="btn primary" :disabled="!password || checking">Permitir</button>
      </div>
      <p class="action">{{ request.action }}</p>
    </form>
  </div>
</template>

<style scoped>
.veil {
  position: fixed;
  inset: 0;
  display: grid;
  place-items: center;
  background: rgba(8, 4, 20, 0);
  transition: background-color 200ms ease;
}
.veil.shown {
  background: rgba(8, 4, 20, 0.6);
}

.card {
  width: min(400px, calc(100vw - 24px));
  box-sizing: border-box;
  padding: 28px 28px 16px;
  border-radius: 24px;
  background: rgba(22, 16, 44, 0.96);
  border: 1px solid var(--lumna-line);
  box-shadow: 0 30px 80px rgba(5, 2, 15, 0.7);
  text-align: center;
  animation: rise 260ms cubic-bezier(0.22, 1, 0.36, 1);
}
.card.shake {
  animation: shake 360ms ease;
}
@keyframes rise {
  from {
    opacity: 0;
    transform: translateY(12px) scale(0.97);
  }
}
@keyframes shake {
  20%,
  60% {
    transform: translateX(-8px);
  }
  40%,
  80% {
    transform: translateX(8px);
  }
}

.lock {
  width: 56px;
  height: 56px;
  margin: 0 auto 14px;
  display: grid;
  place-items: center;
  border-radius: 18px;
  background: linear-gradient(135deg, #a78bfa, #6d28d9);
  color: #fff;
  box-shadow: 0 0 30px rgba(139, 92, 246, 0.45);
}
h1 {
  margin: 0 0 8px;
  font-size: 18px;
  font-weight: 600;
}
.message {
  margin: 0 0 20px;
  font-size: 13px;
  line-height: 1.5;
  color: var(--lumna-muted);
}

.field {
  display: flex;
  align-items: center;
  gap: 8px;
  height: 44px;
  padding: 0 6px 0 14px;
  border-radius: 12px;
  border: 1px solid var(--lumna-line);
  background: rgba(0, 0, 0, 0.3);
  text-align: left;
}
.field:focus-within {
  border-color: var(--lumna-violet);
}
.field.wrong {
  border-color: #f87171;
}
.user {
  font-size: 12px;
  color: var(--lumna-violet-soft);
  padding-right: 8px;
  border-right: 1px solid var(--lumna-line);
}
input {
  flex: 1;
  min-width: 0;
  border: 0;
  outline: 0;
  background: transparent;
  color: var(--lumna-text);
  font: inherit;
  font-size: 15px;
}
.eye {
  width: 32px;
  height: 32px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 8px;
  background: transparent;
  color: var(--lumna-muted);
}
.eye:hover {
  color: var(--lumna-text);
}
.error {
  min-height: 18px;
  margin: 8px 0 14px;
  font-size: 12px;
  color: #fca5a5;
}

.buttons {
  display: flex;
  gap: 10px;
}
.btn {
  flex: 1;
  height: 40px;
  border: 0;
  border-radius: 12px;
  background: rgba(255, 255, 255, 0.08);
  color: var(--lumna-text);
  font: inherit;
  font-size: 14px;
}
.btn:hover:not(:disabled) {
  background: rgba(167, 139, 250, 0.2);
}
.btn.primary {
  background: var(--lumna-violet);
  color: #fff;
}
.btn.primary:hover:not(:disabled) {
  background: #9b72f8;
}
.btn:disabled {
  opacity: 0.5;
}
.action {
  margin: 14px 0 0;
  font-size: 10px;
  color: rgba(165, 156, 201, 0.55);
}
</style>
