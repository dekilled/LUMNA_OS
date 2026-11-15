// Versão "Mídia" do navegador do LUMNA: tela de início (HTML) + um serviço de streaming por vez (CEF Views).
#pragma once
#include "include/cef_base.h"

namespace lumna {
// Cria a janela da Mídia. Chamar na thread de interface, depois de CefInitialize (em OnContextInitialized).
void IniciarMidia();
}  // namespace lumna
