<script setup lang="ts">
// Celular Connect: a ponte entre o celular e o LUMNA do PC. Ainda em construção: aqui fica o
// que ele vai fazer, pra página não prometer botão que não funciona.
import Icon from "../../Icon.vue";

defineProps<{ arg?: string }>();
const FEATURES = [
  { icon: "mic", title: "A Emma do PC no seu bolso", sub: "Avisos quando uma tarefa termina, dá erro ou precisa de você" },
  { icon: "lock", title: "Senha à distância", sub: "Pedidos de senha do PC aparecem aqui; você autoriza do celular" },
  { icon: "power", title: "Comandos de longe", sub: "Desligar, suspender ou mandar a Emma agir, até pela internet" },
  { icon: "play", title: "Controle do modo Mídia", sub: "O celular vira o controle remoto do streaming no PC" },
];
</script>

<template>
  <div class="ms-scroll">
    <div class="hero">
      <span class="big"><Icon name="desktop" :size="40" /></span>
      <h2>Em construção</h2>
      <p>O Connect liga este celular ao LUMNA do seu PC.</p>
    </div>
    <div class="ms-label">O que vem por aí</div>
    <div class="ms-card">
      <div v-for="f in FEATURES" :key="f.title" class="ms-row">
        <span class="ico violet"><Icon :name="f.icon" :size="24" /></span>
        <span class="txt wrap"><strong>{{ f.title }}</strong><small>{{ f.sub }}</small></span>
      </div>
    </div>
  </div>
</template>

<style scoped>
.hero {
  display: flex;
  flex-direction: column;
  align-items: center;
  margin: 8px 0 10px;
  text-align: center;
}
.big {
  width: 84px;
  height: 84px;
  display: grid;
  place-items: center;
  border-radius: 50%;
  background: rgba(139, 92, 246, 0.2);
  color: var(--lumna-violet-soft);
}
h2 {
  margin: 14px 0 4px;
  font-size: 24px;
  font-weight: 500;
}
.hero p {
  margin: 0;
  color: var(--lumna-muted);
}
.wrap strong,
.wrap small {
  white-space: normal !important;
}
</style>
