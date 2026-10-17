<script setup lang="ts">
// Perfil: foto (da galeria do celular), o nome que o LUMNA mostra, usuário e aparelho.
import { onMounted, ref } from "vue";
import Icon from "../../Icon.vue";
import MGallery from "./MGallery.vue";
import { convertFileSrc, profileGet, profileSetName, profileSetPhoto, type Profile } from "../../../system";

defineProps<{ arg?: string }>();
const profile = ref<Profile | null>(null);
const name = ref("");
const gallery = ref(false);
const stamp = ref(Date.now());
const saved = ref(false);
const error = ref("");

async function load() {
  profile.value = await profileGet().catch(() => null);
  name.value = profile.value?.name ?? "";
  stamp.value = Date.now();
}
async function saveName() {
  try {
    await profileSetName(name.value);
    saved.value = true;
    window.setTimeout(() => (saved.value = false), 1500);
  } catch (e) {
    error.value = String(e);
  }
}
async function pick(path: string) {
  gallery.value = false;
  try {
    await profileSetPhoto(path);
  } catch (e) {
    error.value = String(e);
  }
  load();
}
onMounted(load);
</script>

<template>
  <div class="ms-scroll">
    <div class="hero">
      <button class="photo" aria-label="Trocar foto" @click="gallery = true">
        <img v-if="profile?.photo" :src="`${convertFileSrc(profile.photo)}?t=${stamp}`" alt="" />
        <Icon v-else name="user" :size="48" />
        <span class="cam"><Icon name="camera" :size="18" /></span>
      </button>
      <h2>{{ profile?.name }}</h2>
      <p>{{ profile?.user }}@{{ profile?.host }}</p>
    </div>

    <label class="ms-field">
      <small>Nome</small>
      <input v-model="name" placeholder="Como o LUMNA te chama" @keydown.enter="saveName()" />
    </label>
    <div class="ms-actions">
      <button class="ms-btn primary" :disabled="!name.trim() || name === profile?.name" @click="saveName()">
        {{ saved ? "Salvo" : "Salvar nome" }}
      </button>
    </div>

    <div class="ms-label">Conta</div>
    <div class="ms-card">
      <div class="ms-row"><span class="txt"><strong>Usuário</strong></span><span class="value">{{ profile?.user }}</span></div>
      <div class="ms-row"><span class="txt"><strong>Nome do aparelho</strong></span><span class="value">{{ profile?.host }}</span></div>
      <div class="ms-row">
        <span class="txt"><strong>Senha</strong><small>A mesma da tela de bloqueio; troque no terminal com passwd</small></span>
      </div>
    </div>
    <p v-if="error" class="ms-error">{{ error }}</p>
    <MGallery v-if="gallery" @pick="pick" @close="gallery = false" />
  </div>
</template>

<style scoped>
.hero {
  display: flex;
  flex-direction: column;
  align-items: center;
  margin: 8px 0 20px;
}
.photo {
  position: relative;
  width: 112px;
  height: 112px;
  display: grid;
  place-items: center;
  border: 1px solid rgba(255, 255, 255, 0.15);
  border-radius: 50%;
  background: rgba(40, 36, 56, 0.9);
  color: var(--lumna-muted);
}
.photo img {
  width: 100%;
  height: 100%;
  border-radius: 50%;
  object-fit: cover;
}
.cam {
  position: absolute;
  right: 0;
  bottom: 2px;
  width: 36px;
  height: 36px;
  display: grid;
  place-items: center;
  border-radius: 50%;
  background: var(--lumna-violet-soft);
  color: #1a1030;
}
h2 {
  margin: 14px 0 2px;
  font-size: 24px;
  font-weight: 500;
}
.hero p {
  margin: 0;
  color: var(--lumna-muted);
}
</style>
