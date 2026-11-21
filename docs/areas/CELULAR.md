# Área: Celular (a UI mobile e os aparelhos)

Leia antes o [LEIAME.md](LEIAME.md) e `mobile/README.md` (o histórico do porte do OnePlus 6T).

## Escopo

O LUMNA em celular: a **interface** (tela inicial, apps, central, bloqueio, teclado, gestos) e o **porte** para os aparelhos (kernel, boot, rootfs).
A interface é independente do aparelho; o porte de cada aparelho é um projeto de **Hardware** por aparelho (ver abaixo).

## Estado herdado (2026-11-21)

- **OnePlus 6T (`oneplus-fajita`, SDM845, kernel mainline 7.1-rc1)** é o laboratório: Arch Linux ARM nativo, tela, toque, GPU (Adreno 630, freedreno),
  UFS, Wi-Fi, modem registrado, primeira ligação recebida; falta áudio das chamadas, dados móveis, bateria no rootfs, bloqueio e gestos finais.
- **A UI antiga** = Hyprland (config em Lua: `mobile/session/hyprland-mobile.lua`) + `lumna-shell --mobile` (Vue + Rust, `shell/`) + apps GTK/WebKit. O Eric
  diz que **"já não inicia mais, não sei por quê"**. Hipóteses (nenhuma verificada, não há log): (a) o 6T recebeu `pacman -Syu` e o Hyprland novo quebrou a
  config Lua ou o shell; (b) o `lumna-shell` (Vue/WebKit) não sobe mais; (c) o CI do celular **falha desde pelo menos 2026-11-16** (segredo `EMMA_REPO_TOKEN`
  expirado, ver `ATUALIZACAO.md`), então o repositório `[lumna-celular]` ficou parado enquanto o Arch ARM andou; (d) o próprio boot (kernel/initramfs).
- **Desktop atual** = Orbit + Gravity (C), que roda bem. O Vue já foi descartado no desktop por desempenho e espaço (`docs/status/gera.py`).
- O que o Orbit e o Gravity **já têm de celular**: saída `DSI*` tratada como telefone (`output_is_phone`), escala própria (`phone_scale`), mosaico de um
  app por vez e par dividido (`phone_arrange`), gestos de borda (`orbit_gesture_*`, `input.c`), toque (`touch.c`), teclado na tela com input-method
  (`lumna-teclado`), rolagem por dedo com inércia no Gravity (`gv_rolagem.c`), tela de bloqueio (`lumna-bloqueio`), modos e avisos. **Falta**: tela
  inicial/gaveta de apps no formato celular, central (notificações e atalhos), apps recentes, barra de status/navegação, bloqueio com PIN em toque,
  rotação, os apps (Ajustes, Arquivos, Telefone, Contatos) em formato de celular, e **compilar tudo em aarch64 e rodar no 6T** (nunca foi feito).

## Caminho recomendado (precisa da decisão do Eric)

1. **A UI nova do celular é Orbit + Gravity**, a mesma base do desktop, em vez de reviver Hyprland + Vue. Motivos: é o que já roda bem; o Orbit já tem o modo
   telefone; um só código para PC, celular e (um dia) tablet; menos peso. O Vue/Hyprland fica como está (congelado), sem consertar.
2. **Primeiro passo técnico:** compilar Orbit + Gravity para aarch64 (o `pacotes.yml` roda em x86_64; a máquina ARM do `celular.yml` serve) e subir no 6T
   com o `primeira-tela`: provar tela, toque, GPU e o `lumna-barra`/`lumna-lancador` num celular de verdade, antes de desenhar qualquer coisa nova.
3. **Depois, tela por tela, cada uma começando por esboço do Eric** (regra de produto): barra de status + navegação; início e gaveta de apps; central;
   apps recentes; bloqueio; teclado; Ajustes; Telefone.
4. Os apps do desktop ganham um **modo estreito** (já existe para Música e Atlas: `gv_barra_janela`, largura mínima); o celular reaproveita.

## Aparelhos

| Aparelho | SoC | Linux mainline? | Papel |
|---|---|---|---|
| OnePlus 6T | SDM845 | sim (sdm845-mainline), já roda | laboratório |
| **Galaxy Note 10+ do Eric: hardware SM-N975F (Exynos 9825)**, hoje com uma **ROM portada do S22 Ultra** (por isso o Android diz `SM-S908B/DS`; o S22 Ultra tem Exynos 2200, que NÃO é o chip do aparelho) | Exynos 9825 | **praticamente não**: sem o SoC no kernel oficial (clocks, pinctrl, PMIC, UFS), GPU Mali-G76 (a Panfrost cobre o G76, mas sem a base do SoC não há como usá-la), modem Shannon fechado | meta do Eric; projeto grande e incerto, **o Eric quer construir o suporte** preenchendo lacunas e trazendo correções de aparelhos compatíveis |
| (referência) Note 10+ Snapdragon, SM-N975U/U1 | SM8150 (855) | sim | só para comparar |

**Atenção:** para saber o hardware verdadeiro, não confiar no que o Android diz (a ROM portada mente o modelo). Conferir pela etiqueta do aparelho ou no
recovery/bootloader (`fastboot getvar product`, ou o modo Download da Samsung).

## Pendências

1. ~~Decisão do Eric sobre o caminho~~ **decidido (2026-11-21): Orbit + Gravity + todos os apps, um a um**; o Vue/Hyprland antigo é abandonado (o Eric já o refez em C justamente por isso). A UI antiga reiniciava ao carregar o Hyprland (o cursor aparecia, depois reiniciava): não será consertada.
2. Saber por que a versão antiga não inicia **(só se o Eric quiser reviver; se for substituída, não importa)**: do 6T pela USB (`ssh alarm@lumna`):
   `journalctl -b -p warning --no-pager | tail -80`, `systemctl --failed`, `pacman -Q hyprland lumna-shell lumna-mobile`, `ls ~/.local/state/lumna`.
3. Compilar Orbit + Gravity em aarch64 e rodar no 6T.
4. Esboços do Eric da UI do celular.
5. Conserto do CI "Celular" (token expirado) para o `[lumna-celular]` voltar a ser publicado.

## Recebido de outras áreas

(vazio)
