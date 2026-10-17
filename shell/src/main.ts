import { createApp } from "vue";
import App from "./App.vue";
import "./styles.css";

import { setupKeyboardInput } from "./keyboardInput";

createApp(App).mount("#app");

// LUMNA Keyboard nos campos do LUMNA (no PC não faz nada: lá não existe a camada do teclado)
setupKeyboardInput();
