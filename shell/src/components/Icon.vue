<script setup lang="ts">
// Ícones de traço simples, desenhados para 24×24 e herdando a cor do texto.
defineProps<{ name: string; size?: number }>();

const paths: Record<string, string> = {
  wifi: "M2 9a15 15 0 0 1 20 0M5 12.5a10 10 0 0 1 14 0M8.5 16a5 5 0 0 1 7 0M12 19.5h.01",
  ethernet: "M4 8h16v9H4zM8 17v3M12 17v3M16 17v3M8 11h8",
  offline: "M3 3l18 18M8.5 16a5 5 0 0 1 7 0M12 19.5h.01M5 12.5a10 10 0 0 1 5-2.8M2 9a15 15 0 0 1 4.5-3",
  volume: "M4 9v6h4l5 4V5L8 9zM16 9a4 4 0 0 1 0 6M18.5 6.5a8 8 0 0 1 0 11",
  mute: "M4 9v6h4l5 4V5L8 9zM17 9l5 6M22 9l-5 6",
  battery: "M3 7h16v10H3zM21 10v4",
  bolt: "M11 7l-3 5h4l-2 5",
  search: "M10.5 18a7.5 7.5 0 1 0 0-15 7.5 7.5 0 0 0 0 15zM16 16l5 5",
  grid: "M4 4h6v6H4zM14 4h6v6h-6zM4 14h6v6H4zM14 14h6v6h-6z",
  close: "M6 6l12 12M18 6L6 18",
  app: "M5 5h14v14H5z",
  desktop: "M3 4h18v12H3zM8 20h8M12 16v4",
  gamepad: "M7 8h10a4 4 0 0 1 4 4v1a3 3 0 0 1-5.2 2.1L14.5 14h-5l-1.3 1.1A3 3 0 0 1 3 13v-1a4 4 0 0 1 4-4zM8 10.5v3M6.5 12h3M15.5 11.5h.01M17.5 13h.01",
  play: "M12 21a9 9 0 1 0 0-18 9 9 0 0 0 0 18zM10 8.5v7l5.5-3.5z",
  screens: "M2 5h9v7H2zM13 5h9v7h-9zM6 15v3M17 15v3M4 18h4M15 18h4",
  extend: "M2 5h20v10H2zM8 19h8M12 15v4",
};
</script>

<template>
  <svg
    :width="size ?? 16"
    :height="size ?? 16"
    viewBox="0 0 24 24"
    fill="none"
    stroke="currentColor"
    stroke-width="1.8"
    stroke-linecap="round"
    stroke-linejoin="round"
    aria-hidden="true"
  >
    <path :d="paths[name] ?? paths.app" />
  </svg>
</template>
