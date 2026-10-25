# Roteiro: LUMNA OS no Orbit com o Gravity

O shell em Vue fica como está. O LUMNA novo é o **Orbit** (compositor) com o **Gravity** (a
interface em C) por cima, feito por etapas que terminam sempre em algo que roda — primeiro no
Orbit dentro do PC, depois no celular. Cada peça do desktop é um processo pequeno: se uma cai, as
outras ficam de pé.

Marque `[x]` ao terminar; o que estiver a meio leva `[~]`.

## 1. A base do Gravity

- [x] Paleta nova do LUMNA (tokens) e componentes nos estados certos
- [x] Botões, deslizante, interruptor, campo de texto, linha de ajuste, abas, barra da janela
- [x] Rolagem: arrastar com o dedo, inércia, elástico nas pontas, roda do rato, barra que aparece
- [x] Imagens: PNG/JPEG/WebP/SVG descodificadas em threads, no tamanho da tela, em cache
- [x] Popups e menus presos a um botão (xdg-popup), que fecham ao tocar fora
- [x] Lista suspensa (dropdown), como na tela de Rede
- [x] Foco pelo teclado entre controles (Tab, setas, Enter) com o anel de foco
- [x] Texto que quebra em várias linhas e campo de várias linhas
- [x] Ícones dos apps pelo tema de ícones (freedesktop) para o lançador

## 2. O desktop no Orbit

- [x] Papel de parede (camada de fundo) — exemplo `papel`; falta trocar ao vivo
- [~] Topbar: LUMNA, modo, indicador de áreas, Wi-Fi, som, Bluetooth, relógio — exemplo `barra`
- [x] Dock: apps fixos, abertos, indicador do ativo — exemplo `dock`
  - [x] Menu do botão direito (ou segurar): abrir nova janela, fixar/remover, fechar, preferências
  - [ ] Lista das janelas ao passar o mouse num app com várias (título; miniaturas quando o Orbit
        for para o wlroots 0.19, que captura janelas)
  - [ ] Preferências: pílula ou barra de borda a borda; só no monitor principal ou em todos
  - [ ] Janela maximizada: ocultar (com a faixa que a traz de volta) ou esticar, por monitor —
        precisa do "maximizar" no Orbit
  - [ ] Arrastar para reordenar
- [~] Menus da topbar: Bluetooth, Áudio, Rede (as telas novas) — o do Bluetooth já abre (dados de mentira)
- [~] Lançador (grade de apps com busca) — a lista dos .desktop já existe no exemplo `apps`
- [ ] Notificações (servidor org.freedesktop.Notifications) e o centro delas
- [ ] Tela de bloqueio com PIN
- [~] Orbit: o que o desktop pedir pelo caminho — já atende ativar e fechar pelo foreign-toplevel (dock); falta as áreas

## 3. Serviços do sistema

- [x] Som (libpulse / pipewire-pulse)
- [ ] Rede (NetworkManager por D-Bus)
- [ ] Bluetooth (BlueZ por D-Bus)
- [ ] Energia e bateria (UPower)
- [ ] Brilho e tela

## 4. Apps

- [~] Painel de controle (o layout da tela nova já está no exemplo `painel`)
- [~] Rede — a janela da tela nova no exemplo `rede` (falta o NetworkManager)
- [~] Celular Connect — a área de transferência no exemplo `transferencia` (falta a ligação ao celular)
- [ ] Mixer como app e como camada (já existe no exemplo `mixer`)

## 5. O celular

- [ ] Descobrir o crashdump: depois de cair, ler `/sys/fs/pstore/*` e `journalctl -b -1`; o
      Samsung ir para o RAM dump é pânico do kernel (não é só falta de memória: essa mata o
      processo, não o aparelho). Ver se acontece só com o shell atual ou também parado no TTY.
- [ ] Orbit e Gravity em aarch64 no CI (o workflow Celular)
- [ ] Orbit no backend DRM do aparelho, com libseat
- [ ] Gestos de borda, teclado na tela (já há o relay do input-method), rotação
- [ ] Medir memória e CPU lá (cada app Gravity usa uns 16 MB aqui no PC)
- [ ] Trocar a sessão do celular pelo Orbit + desktop novo
