<script setup lang="ts">
import { computed, nextTick, onMounted, onUnmounted, ref } from "vue";
import Icon from "./Icon.vue";
import {
  greeterLogin,
  greeterPower,
  greeterSessions,
  greeterUsers,
  status,
  type GreeterSession,
  type GreeterUser,
  type Status,
} from "../system";

// Tela de login do LUMNA (docs/visual/12-login.jpg). Fala com o greetd pelo Rust.
const users = ref<GreeterUser[]>([]);
const sessions = ref<GreeterSession[]>([]);
const selected = ref<GreeterUser | null>(null);
const session = ref<string | null>(null);
const picking = ref(false);
const sessionMenu = ref(false);
const password = ref("");
const reveal = ref(false);
const busy = ref(false);
const error = ref("");
const shake = ref(false);
const leaving = ref(false);
const large = ref(false);
const now = ref(new Date());
const sys = ref<Status | null>(null);
const input = ref<HTMLInputElement>();

const time = computed(() => new Intl.DateTimeFormat("pt-BR", { hour: "2-digit", minute: "2-digit" }).format(now.value));
const display = (u: GreeterUser) => (u.name || u.username).split(" ")[0];
const sessionName = computed(() => sessions.value.find((s) => s.id === session.value)?.name ?? "LUMNA · Hyprland");
const networkIcon = computed(() => ({ wifi: "wifi", ethernet: "ethernet", none: "offline" })[sys.value?.network ?? "none"]);

async function focus() {
  await nextTick();
  if (!leaving.value) input.value?.focus();
}

function choose(u: GreeterUser) {
  selected.value = u;
  picking.value = false;
  password.value = "";
  error.value = "";
  focus();
}

async function submit() {
  if (!selected.value || !password.value || busy.value) return;
  busy.value = true;
  error.value = "";
  try {
    await greeterLogin(selected.value.username, password.value, session.value);
    leaving.value = true;
  } catch (e) {
    error.value = String(e);
    password.value = "";
    shake.value = false;
    await nextTick();
    shake.value = true;
  } finally {
    busy.value = false;
  }
  // Só depois de reativar o campo: um campo desativado não recebe o foco.
  focus();
}

let clock: number | undefined;
let statusTimer: number | undefined;
onMounted(async () => {
  clock = window.setInterval(() => (now.value = new Date()), 1000);
  const refresh = async () => (sys.value = await status().catch(() => null));
  refresh();
  statusTimer = window.setInterval(refresh, 5000);
  [users.value, sessions.value] = await Promise.all([greeterUsers(), greeterSessions()]);
  session.value = sessions.value[0]?.id ?? null;
  if (users.value.length) choose(users.value[0]);
});
onUnmounted(() => {
  window.clearInterval(clock);
  window.clearInterval(statusTimer);
});
</script>

<template>
  <div class="greeter" :class="{ leaving, large }" @click.self="sessionMenu = false; focus()">
    <header class="top">
      <span class="brand">
        <svg viewBox="0 0 24 24" aria-hidden="true">
          <path d="M12 1.5 19 9l-7 13.5L5 9z" fill="#a78bfa" />
          <path d="M12 1.5 15 9l-3 13.5L9 9z" fill="#fff" opacity=".3" />
        </svg>
        LUMNA
      </span>
      <span class="status">
        <Icon :name="networkIcon" :size="20" />
        <Icon v-if="sys?.volume !== null && sys?.volume !== undefined" :name="sys.muted ? 'mute' : 'volume'" :size="20" />
        <span class="time">{{ time }}</span>
      </span>
    </header>

    <!-- Escolher a conta -->
    <main v-if="picking || !selected" class="card">
      <h1>{{ users.length ? "Quem vai entrar?" : "Nenhuma conta neste computador" }}</h1>
      <div class="people">
        <button v-for="u in users" :key="u.username" @click="choose(u)">
          <span class="avatar small"><Icon name="user" :size="34" /></span>
          {{ u.name || u.username }}
        </button>
      </div>
    </main>

    <!-- Senha -->
    <main v-else class="card" :class="{ shake }" @animationend="shake = false">
      <div class="avatar"><Icon name="user" :size="58" /></div>
      <h1>{{ display(selected) }}</h1>
      <p class="hello">Bem-vindo de volta</p>
      <form @submit.prevent="submit">
        <label class="field" :class="{ wrong: error }">
          <Icon name="lock" :size="18" />
          <input
            ref="input"
            v-model="password"
            :type="reveal ? 'text' : 'password'"
            placeholder="Senha"
            autocomplete="off"
            :disabled="busy"
            @keydown.enter.prevent="submit"
          />
          <button type="button" class="eye" :title="reveal ? 'Esconder senha' : 'Mostrar senha'" @click="reveal = !reveal; focus()">
            <Icon name="eye" :size="18" />
          </button>
        </label>
        <p class="error">{{ error }}</p>
        <button class="enter" type="submit" :disabled="!password || busy">
          <span v-if="busy" class="spinner" />
          <template v-else>Entrar</template>
        </button>
      </form>
      <button v-if="users.length > 1" class="switch" @click="picking = true">Trocar usuário</button>
    </main>

    <!-- Sessão -->
    <div class="session">
      <button @click="sessionMenu = !sessionMenu">
        {{ sessionName }} <Icon name="chevron-down" :size="16" />
      </button>
      <div v-if="sessionMenu" class="menu">
        <button v-for="s in sessions" :key="s.id" :class="{ on: s.id === session }" @click="session = s.id; sessionMenu = false; focus()">
          {{ s.name }}
        </button>
      </div>
    </div>

    <!-- Acessibilidade e energia -->
    <footer class="power">
      <button :class="{ on: large }" title="Letras maiores" @click="large = !large; focus()"><Icon name="accessibility" :size="22" /></button>
      <span class="sep" />
      <button title="Reiniciar" @click="greeterPower('reboot')"><Icon name="restore" :size="22" /></button>
      <span class="sep" />
      <button title="Desligar" @click="greeterPower('poweroff')"><Icon name="power" :size="22" /></button>
    </footer>
  </div>
</template>

<style scoped>
.greeter {
  position: fixed;
  inset: 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  /* O wallpaper (swaybg) aparece por trás; só um leve escurecimento no centro. */
  background: radial-gradient(60% 55% at 50% 52%, rgba(8, 5, 18, 0.35), transparent 75%);
  color: #f2f0f8;
  transition: opacity 320ms ease;
}
.greeter.large {
  zoom: 1.25;
}
.greeter.leaving {
  opacity: 0;
}
button {
  font: inherit;
  color: inherit;
}

.top {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  height: 60px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 32px 0 38px;
}
.brand {
  display: flex;
  align-items: center;
  gap: 12px;
  font-weight: 600;
  letter-spacing: 0.16em;
  font-size: 17px;
}
.brand svg {
  width: 20px;
  height: 26px;
}
.status {
  display: flex;
  align-items: center;
  gap: 26px;
}
.time {
  font-size: 17px;
}

.card {
  width: 440px;
  display: flex;
  flex-direction: column;
  align-items: center;
  margin-top: -40px;
}
.card.shake {
  animation: shake 360ms ease;
}
@keyframes shake {
  20%,
  60% {
    transform: translateX(-10px);
  }
  40%,
  80% {
    transform: translateX(10px);
  }
}
.avatar {
  width: 120px;
  height: 120px;
  border-radius: 50%;
  display: grid;
  place-items: center;
  background: #1c1c22;
  border: 1px solid rgba(255, 255, 255, 0.08);
  color: #f2f0f8;
}
.avatar.small {
  width: 84px;
  height: 84px;
}
h1 {
  margin: 18px 0 0;
  font-size: 30px;
  font-weight: 400;
}
.hello {
  margin: 6px 0 26px;
  font-size: 16px;
  color: rgba(242, 240, 248, 0.7);
}
form {
  width: 100%;
}
.field {
  height: 56px;
  display: flex;
  align-items: center;
  gap: 14px;
  padding: 0 14px 0 20px;
  border-radius: 10px;
  background: rgba(22, 20, 32, 0.82);
  border: 1px solid rgba(167, 139, 250, 0.75);
  color: rgba(242, 240, 248, 0.75);
}
.field.wrong {
  border-color: #f87171;
}
.field input {
  flex: 1;
  min-width: 0;
  border: 0;
  outline: 0;
  background: transparent;
  color: #fff;
  font: inherit;
  font-size: 16px;
}
.field input::placeholder {
  color: rgba(242, 240, 248, 0.5);
}
.eye {
  display: grid;
  place-items: center;
  width: 34px;
  height: 34px;
  border: 0;
  border-radius: 8px;
  background: transparent;
}
.eye:hover {
  background: rgba(255, 255, 255, 0.08);
}
.error {
  min-height: 20px;
  margin: 6px 0 4px;
  font-size: 13px;
  color: #fca5a5;
  text-align: center;
}
.enter {
  width: 100%;
  height: 52px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 10px;
  background: #4f3a78;
  font-size: 17px;
  transition: background-color 160ms ease;
}
.enter:hover:not(:disabled) {
  background: #5d4590;
}
.enter:disabled {
  opacity: 0.6;
}
.switch {
  margin-top: 22px;
  border: 0;
  background: transparent;
  font-size: 15px;
  color: rgba(242, 240, 248, 0.7);
}
.switch:hover {
  color: #fff;
}
.people {
  display: flex;
  flex-wrap: wrap;
  justify-content: center;
  gap: 22px;
  margin-top: 30px;
}
.people button {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 12px;
  padding: 14px;
  border: 0;
  border-radius: 16px;
  background: transparent;
  font-size: 15px;
}
.people button:hover {
  background: rgba(255, 255, 255, 0.06);
}

.session {
  position: absolute;
  left: 40px;
  bottom: 42px;
}
.session > button {
  height: 42px;
  display: flex;
  align-items: center;
  gap: 14px;
  padding: 0 16px 0 18px;
  border-radius: 8px;
  border: 1px solid rgba(255, 255, 255, 0.1);
  background: rgba(16, 15, 22, 0.75);
  font-size: 15px;
  color: rgba(242, 240, 248, 0.85);
}
.menu {
  position: absolute;
  bottom: 50px;
  left: 0;
  min-width: 100%;
  padding: 6px;
  border-radius: 10px;
  background: rgba(22, 20, 32, 0.96);
  border: 1px solid rgba(255, 255, 255, 0.1);
}
.menu button {
  width: 100%;
  height: 36px;
  padding: 0 12px;
  border: 0;
  border-radius: 7px;
  background: transparent;
  text-align: left;
  white-space: nowrap;
  font-size: 14px;
}
.menu button:hover,
.menu button.on {
  background: rgba(139, 92, 246, 0.3);
}

.power {
  position: absolute;
  right: 36px;
  bottom: 40px;
  display: flex;
  align-items: center;
  gap: 18px;
}
.power button {
  width: 44px;
  height: 44px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 12px;
  background: transparent;
  color: rgba(242, 240, 248, 0.9);
}
.power button:hover,
.power button.on {
  background: rgba(255, 255, 255, 0.1);
}
.sep {
  width: 1px;
  height: 24px;
  background: rgba(255, 255, 255, 0.15);
}
.spinner {
  width: 18px;
  height: 18px;
  border-radius: 50%;
  border: 2px solid rgba(255, 255, 255, 0.3);
  border-top-color: #fff;
  animation: spin 800ms linear infinite;
}
@keyframes spin {
  to {
    transform: rotate(360deg);
  }
}
</style>
