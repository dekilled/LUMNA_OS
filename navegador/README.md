# lumna-navegador (CEF)

Navegador do LUMNA. Hoje existe a **Mídia** (versão simples, para a TV/sala): início com "Continuar assistindo" e os atalhos dos serviços, um serviço de streaming por vez e um menu de troca. Veja `docs/NAVEGADOR.md` para as decisões e o desenho.

## Compilar e abrir
```
# precisa de um CEF extraído (o scripts/teste-cef-codecs.sh baixa um com H.264/AAC em ~/.cache/lumna-cef-teste/godwinmix)
sudo pacman -S --needed base-devel cmake ninja libx11 gtk3
CEF_ROOT=~/.cache/lumna-cef-teste/godwinmix sh navegador/dev.sh          # compila, monta navegador/build/stage e abre a Mídia
CEF_ROOT=... sh navegador/dev.sh compilar                                # só compila e monta
```
O Widevine é baixado pelo próprio CEF na primeira vez (de 1 a 3 minutos, depois **reinicie o app**); fica no perfil `~/.local/share/lumna/navegador/perfil`.

## Teclas (provisórias)
| Tecla | O que faz |
|---|---|
| ← → ↑ ↓ Enter | escolhe e abre (no início e no menu) |
| **F2** | abre/fecha o menu de serviços (o site encolhe e segue tocando) |
| **Esc** (no menu) | volta ao vídeo |
| **Alt+Home** | volta ao início |

## Variáveis de ambiente
`LUMNA_NAV_DADOS` (pasta de dados), `LUMNA_NAV_SERVICOS` (JSON `[{"id","nome","url"}]` com outra lista de serviços), `LUMNA_NAV_TELA_CHEIA=1`, `LUMNA_NAV_OZONE=wayland` (padrão: x11), `LUMNA_NAV_SEM_GPU=1`, `LUMNA_NAV_DEBUG=1` (log `NAV ...` no terminal), `LUMNA_NAV_LARGURA/ALTURA`, `LUMNA_NAV_UI`.

## Teste automático
`sh navegador/testes/midia.sh <pasta do orbit> navegador/build/stage` (Orbit sem tela + XWayland; só pelo teclado).

## Como é feito (e por quê)
- **Uma janela, duas camadas** (CEF Views, sem moldura): a interface em HTML (`ui/midia/`) por baixo e o site do serviço por cima. A janela é estilo **Chrome** (precisa dele: é o que baixa o Widevine sozinho) e esse estilo só aceita **um** BrowserView Chrome (o site); a interface é um BrowserView **Alloy**. **A ordem importa:** o site entra primeiro e a interface depois (e desce para o fundo com `ReorderChildView`); ao contrário, o CEF recusa ("Cannot add multiple Chrome style BrowserViews").
- **Ponte JS↔C++** (`window.cefQuery`) só existe na nossa interface (`file://`), nunca nos sites.
- **Um serviço vivo por vez:** trocar de serviço guarda o último endereço, o progresso do vídeo e um print (lidos pelo DevTools: `Runtime.evaluate` e `Page.captureScreenshot`) em `midia.json` e `thumbs/`, e reaproveita a mesma visão.
