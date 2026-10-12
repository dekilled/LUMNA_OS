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
  phone: "M8 2h8a2 2 0 0 1 2 2v16a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2zM11 18h2",
  folder: "M3 6a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v10a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z",
  lock: "M6 11h12v10H6zM8.5 11V8a3.5 3.5 0 0 1 7 0v3",
  refresh: "M20 12a8 8 0 1 1-2.3-5.7M20 4v5h-5",
  check: "M5 12.5l4.5 4.5L19 7",
  eye: "M2 12s3.6-7 10-7 10 7 10 7-3.6 7-10 7S2 12 2 12zM12 15a3 3 0 1 0 0-6 3 3 0 0 0 0 6z",
  bluetooth: "M7 7l10 10-5 4V3l5 4L7 17",
  printer: "M6 9V3h12v6M6 18H4v-7h16v7h-2M7 14h10v7H7z",
  headphones: "M4 15v-3a8 8 0 0 1 16 0v3M4 15h3v6H5a1 1 0 0 1-1-1zM20 15h-3v6h2a1 1 0 0 0 1-1z",
  keyboard: "M3 6h18v12H3zM7 10h.01M11 10h.01M15 10h.01M7 14h10",
  mouse: "M12 3a6 6 0 0 1 6 6v6a6 6 0 0 1-12 0V9a6 6 0 0 1 6-6zM12 7v4",
  mic: "M12 3a3 3 0 0 1 3 3v6a3 3 0 0 1-6 0V6a3 3 0 0 1 3-3zM5 11a7 7 0 0 0 14 0M12 18v3",
  info: "M12 21a9 9 0 1 0 0-18 9 9 0 0 0 0 18zM12 11v5M12 8h.01",
  minimize: "M6 12h12",
  maximize: "M6 6h12v12H6z",
  star: "M12 3l2.8 5.7 6.2.9-4.5 4.4 1 6.2L12 17.3 6.5 20.2l1-6.2L3 9.6l6.2-.9z",
  trash: "M4 7h16M10 11v6M14 11v6M6 7l1 13h10l1-13M9 7V4h6v3",
  plus: "M12 5v14M5 12h14",
  music: "M9 18V5l12-2v13M9 18a3 3 0 1 1-6 0 3 3 0 0 1 6 0zM21 16a3 3 0 1 1-6 0 3 3 0 0 1 6 0z",
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
