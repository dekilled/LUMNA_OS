<script setup lang="ts">
import { ref } from "vue";
import { on } from "./system";
import { getCurrentWebviewWindow } from "@tauri-apps/api/webviewWindow";
import AuthDialog from "./components/AuthDialog.vue";
import Desktop from "./components/Desktop.vue";
import Dock from "./components/Dock.vue";
import EmmaPanel from "./components/EmmaPanel.vue";
import EmmaAssist from "./components/emma/EmmaAssist.vue";
import EmmaApp from "./components/emma/EmmaApp.vue";
import GalleryApp from "./components/gallery/GalleryApp.vue";
import MusicApp from "./components/music/MusicApp.vue";
import CaptureLayer from "./components/capture/CaptureLayer.vue";
import RecordingPill from "./components/capture/RecordingPill.vue";
import FilesApp from "./components/files/FilesApp.vue";
import MobileFiles from "./components/files/MobileFiles.vue";
import GreeterApp from "./components/GreeterApp.vue";
import InstallerApp from "./components/InstallerApp.vue";
import Launcher from "./components/Launcher.vue";
import ModeScreen from "./components/ModeScreen.vue";
import MobileHome from "./components/mobile/MobileHome.vue";
import MobileKeyboard from "./components/mobile/MobileKeyboard.vue";
import MobileNav from "./components/mobile/MobileNav.vue";
import MobileEdge from "./components/mobile/MobileEdge.vue";
import MobileCenter from "./components/mobile/MobileCenter.vue";
import MobileRecents from "./components/mobile/MobileRecents.vue";
import MobileCall from "./components/mobile/MobileCall.vue";
import MobileLock from "./components/mobile/MobileLock.vue";
import MobileAsk from "./components/mobile/MobileAsk.vue";
import MobilePower from "./components/mobile/MobilePower.vue";
import MobileVolume from "./components/mobile/MobileVolume.vue";
import PhoneApp from "./components/phone/PhoneApp.vue";
import RemoteApp from "./components/remote/RemoteApp.vue";
import MobileStatus from "./components/mobile/MobileStatus.vue";
import Notifications from "./components/Notifications.vue";
import RestScreen from "./components/RestScreen.vue";
import PowerMenu from "./components/PowerMenu.vue";
import SettingsApp from "./components/settings/SettingsApp.vue";
import StoreApp from "./components/store/StoreApp.vue";
import Switcher from "./components/Switcher.vue";
import TextMenu from "./components/TextMenu.vue";
import TrayMenu from "./components/TrayMenu.vue";
import TvBar from "./components/TvBar.vue";
import Topbar from "./components/Topbar.vue";
import WifiMenu from "./components/WifiMenu.vue";

// Cada janela do shell carrega o mesmo app; o rótulo diz qual peça desenhar.
const label = getCurrentWebviewWindow().label;
// as camadas do celular (o Rust põe antes da página carregar)
const mobile = (window as { __LUMNA_MOBILE?: boolean }).__LUMNA_MOBILE === true;
// janela estreita (celular): os apps com layout próprio de celular
// (o compositor pode redimensionar a janela depois de abrir: acompanha)
const narrow = ref(window.innerWidth < 600);
// Celular: nos campos do LUMNA o teclado fica por cima da janela (sem redimensionar = sem flick);
// a página encolhe por dentro (--app-h) e o campo em foco rola pra ficar visível.
if (["settings", "phone", "remote", "store", "installer", "emma-app", "gallery", "music"].includes(label) || label.startsWith("files-")) {
  on<{ shown: boolean; lumna: boolean; height: number }>("lumna://keyboard", (k) => {
    // a janela já termina 26 px acima da borda (a faixa da barrinha)
    const inset = k.shown && k.lumna ? Math.max(0, k.height - 26) : 0;
    document.documentElement.style.setProperty("--app-h", inset ? `calc(100vh - ${inset}px)` : "100vh");
    // folhas, avisos e barras presas no fundo da tela sobem junto (o campo fica acima do teclado)
    document.documentElement.style.setProperty("--kb-inset", `${inset}px`);
    if (inset) requestAnimationFrame(() => (document.activeElement as HTMLElement | null)?.scrollIntoView?.({ block: "center" }));
  });
}
window.addEventListener("resize", () => (narrow.value = window.innerWidth < 600));
</script>

<template>
  <Desktop v-if="label.startsWith('desktop-')" />
  <Topbar v-else-if="label.startsWith('topbar-')" />
  <RestScreen v-else-if="label.startsWith('rest-')" />
  <Dock v-else-if="label.startsWith('dock-')" />
  <Launcher v-else-if="label === 'launcher'" />
  <Notifications v-else-if="label === 'notifications'" />
  <ModeScreen v-else-if="label.startsWith('mode-')" />
  <Switcher v-else-if="label === 'switcher'" />
  <TvBar v-else-if="label === 'tvbar'" />
  <WifiMenu v-else-if="label === 'wifi'" />
  <PowerMenu v-else-if="label === 'power'" />
  <TrayMenu v-else-if="label === 'traymenu'" />
  <EmmaAssist v-else-if="label === 'emma' && mobile" />
  <EmmaPanel v-else-if="label === 'emma'" />
  <EmmaApp v-else-if="label === 'emma-app'" />
  <GalleryApp v-else-if="label === 'gallery'" />
  <MusicApp v-else-if="label === 'music'" />
  <CaptureLayer v-else-if="label === 'capture'" />
  <RecordingPill v-else-if="label === 'recording'" />
  <SettingsApp v-else-if="label === 'settings'" />
  <StoreApp v-else-if="label === 'store'" />
  <AuthDialog v-else-if="label === 'auth'" />
  <MobileFiles v-else-if="label.startsWith('files-') && narrow" />
  <FilesApp v-else-if="label.startsWith('files-')" />
  <InstallerApp v-else-if="label === 'installer'" />
  <GreeterApp v-else-if="label === 'greeter'" />
  <MobileHome v-else-if="label === 'mobile-home'" />
  <MobileStatus v-else-if="label === 'mobile-status'" />
  <MobileNav v-else-if="label === 'mobile-nav'" />
  <MobileEdge v-else-if="label === 'mobile-edge-left'" side="left" />
  <MobileEdge v-else-if="label === 'mobile-edge-right'" side="right" />
  <MobileCenter v-else-if="label === 'mobile-center'" />
  <MobileRecents v-else-if="label === 'mobile-recents'" />
  <MobileCall v-else-if="label === 'mobile-call'" />
  <MobileLock v-else-if="label === 'mobile-lock'" />
  <MobileAsk v-else-if="label === 'mobile-ask'" />
  <MobilePower v-else-if="label === 'mobile-power'" />
  <MobileVolume v-else-if="label === 'mobile-volume'" />
  <PhoneApp v-else-if="label === 'phone'" />
  <RemoteApp v-else-if="label === 'remote'" />
  <MobileKeyboard v-else-if="label === 'keyboard'" />
  <TextMenu />
</template>
