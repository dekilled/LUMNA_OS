<script setup lang="ts">
import { getCurrentWebviewWindow } from "@tauri-apps/api/webviewWindow";
import AuthDialog from "./components/AuthDialog.vue";
import Desktop from "./components/Desktop.vue";
import Dock from "./components/Dock.vue";
import EmmaPanel from "./components/EmmaPanel.vue";
import FilesApp from "./components/files/FilesApp.vue";
import GreeterApp from "./components/GreeterApp.vue";
import InstallerApp from "./components/InstallerApp.vue";
import Launcher from "./components/Launcher.vue";
import ModeScreen from "./components/ModeScreen.vue";
import MobileHome from "./components/mobile/MobileHome.vue";
import MobileKeyboard from "./components/mobile/MobileKeyboard.vue";
import MobileNav from "./components/mobile/MobileNav.vue";
import MobileCenter from "./components/mobile/MobileCenter.vue";
import MobileRecents from "./components/mobile/MobileRecents.vue";
import MobileCall from "./components/mobile/MobileCall.vue";
import MobileLock from "./components/mobile/MobileLock.vue";
import MobileAsk from "./components/mobile/MobileAsk.vue";
import MobilePower from "./components/mobile/MobilePower.vue";
import PhoneApp from "./components/phone/PhoneApp.vue";
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
  <EmmaPanel v-else-if="label === 'emma'" />
  <SettingsApp v-else-if="label === 'settings'" />
  <StoreApp v-else-if="label === 'store'" />
  <AuthDialog v-else-if="label === 'auth'" />
  <FilesApp v-else-if="label.startsWith('files-')" />
  <InstallerApp v-else-if="label === 'installer'" />
  <GreeterApp v-else-if="label === 'greeter'" />
  <MobileHome v-else-if="label === 'mobile-home'" />
  <MobileStatus v-else-if="label === 'mobile-status'" />
  <MobileNav v-else-if="label === 'mobile-nav'" />
  <MobileCenter v-else-if="label === 'mobile-center'" />
  <MobileRecents v-else-if="label === 'mobile-recents'" />
  <MobileCall v-else-if="label === 'mobile-call'" />
  <MobileLock v-else-if="label === 'mobile-lock'" />
  <MobileAsk v-else-if="label === 'mobile-ask'" />
  <MobilePower v-else-if="label === 'mobile-power'" />
  <PhoneApp v-else-if="label === 'phone'" />
  <MobileKeyboard v-else-if="label === 'keyboard'" />
  <TextMenu />
</template>
