<script setup lang="ts">
import { computed, onMounted, ref } from "vue";
import { systemInfo, type SystemInfo } from "../../system";

const info = ref<SystemInfo | null>(null);

const gb = (bytes: number) => {
  const value = bytes / 1024 ** 3;
  return `${value >= 100 ? Math.round(value) : value.toFixed(1).replace(".", ",")} GB`;
};

const uptime = computed(() => {
  const s = info.value?.uptime ?? 0;
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  return h ? `${h} h ${m} min` : `${m} min`;
});

const diskShare = computed(() => (info.value?.disk_total ? (info.value.disk_used / info.value.disk_total) * 100 : 0));

onMounted(async () => (info.value = await systemInfo()));
</script>

<template>
  <div class="hero">
    <svg class="crystal" viewBox="0 0 24 24" aria-hidden="true">
      <defs>
        <linearGradient id="sys-g" x1="0" y1="0" x2="1" y2="1">
          <stop offset="0" stop-color="#c4b5fd" />
          <stop offset="1" stop-color="#7c3aed" />
        </linearGradient>
      </defs>
      <path d="M12 1.5 19 9l-7 13.5L5 9z" fill="url(#sys-g)" />
      <path d="M12 1.5 15 9l-3 13.5L9 9z" fill="#ffffff" opacity=".25" />
    </svg>
    <div>
      <h1>LUMNA OS</h1>
      <p class="lead">Seu espaço. Seus modos. Sua Emma.</p>
    </div>
  </div>

  <div v-if="info" class="card padded">
    <dl>
      <dt>Nome do computador</dt>
      <dd>{{ info.hostname || "-" }}</dd>
      <dt>Processador</dt>
      <dd>{{ info.cpu || "-" }} <span v-if="info.cores" class="muted">({{ info.cores }} threads)</span></dd>
      <dt>Placa de vídeo</dt>
      <dd>{{ info.gpus.join(" · ") || "-" }}</dd>
      <dt>Memória</dt>
      <dd>{{ gb(info.memory) }}</dd>
      <dt>Disco</dt>
      <dd>
        {{ gb(info.disk_used) }} usados de {{ gb(info.disk_total) }}
        <div class="bar"><span :style="{ width: `${diskShare}%` }" /></div>
      </dd>
      <dt>Base</dt>
      <dd>{{ info.os }} · kernel {{ info.kernel }}</dd>
      <dt>Ligado há</dt>
      <dd>{{ uptime }}</dd>
    </dl>
  </div>
</template>

<style scoped>
.hero {
  display: flex;
  align-items: center;
  gap: 18px;
  margin-bottom: 8px;
}
.crystal {
  width: 56px;
  height: 56px;
  filter: drop-shadow(0 0 14px rgba(139, 92, 246, 0.7));
}
.muted {
  color: var(--lumna-muted);
}
.bar {
  margin-top: 8px;
  height: 6px;
  max-width: 320px;
  border-radius: 3px;
  background: rgba(255, 255, 255, 0.1);
  overflow: hidden;
}
.bar span {
  display: block;
  height: 100%;
  background: linear-gradient(90deg, #a78bfa, #7c3aed);
}
</style>
