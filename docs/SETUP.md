# Pré-setup (lumna-setup)

A configuração de primeiro uso, que vem depois da instalação. Esboços: as cinco telas que o Eric mandou (Boas-vindas, Rede e som, Contas, Emma, Connect).
Código: `gravity/exemplos/setup.c` (a janela) e `gravity/servicos/setup.c` (o que grava e aplica), `gravity/servicos/emma_ativa.[ch]` (a Emma ligada/desligada).

## Quando aparece

O `lumna-orbit-session` roda `lumna-setup --login` 4 s depois do desktop subir. Por usuário (os marcadores ficam em `~/.config/lumna/setup.ini`):

| Situação | O que o login faz |
|---|---|
| Nunca abriu | Abre o assistente (e conta a abertura) |
| Já abriu e não terminou (fechou pela metade) | Só um **aviso** ("Termine de configurar o seu LUMNA", com o botão *Continuar a configuração*) |
| Terminou (ou "Começar a usar" / "Pular e terminar") | Nada, nunca mais |
| **Não mostrar de novo** ligado (interruptor na lateral) | Nada: nem janela, nem aviso |

Dá para abrir de novo quando quiser: **Configuração inicial** no lançador (`lumna-setup`), ou `lumna-setup --passo N` (1 a 5).

## As etapas

1. **Boas-vindas**: perfil de uso (Uso diário, Gaming, Mídia, Desenvolvimento), idioma, teclado, fuso (busca com sugestões) e formato da hora.
   O perfil define o **modo em que o sistema começa** (`modos.ini` → `ao_ligar`: Gaming→Gaming, Mídia→Mídia, os outros→Desktop) e grava **sugestões de apps**
   em `sugestoes.ini` (nada é instalado sozinho). Idioma (`localectl`) e fuso (`timedatectl`) pedem a senha do administrador pelo polkit, em segundo plano.
2. **Rede e som**: Wi-Fi pelo mesmo serviço do Painel (lista, senha, conectar, ligar/desligar), saída e entrada de áudio padrão, "Testar som".
   Sem NetworkManager ou PipeWire a etapa avisa e dá para pular.
3. **Contas**: o **e-mail** só é *guardado* (`setup.ini [email]`, com a configuração manual de IMAP/SMTP) para o app de e-mail usar quando existir.
   A **nuvem** (Google Drive, OneDrive, Dropbox) usa o `rclone`: "Conectar conta" roda `rclone config create lumna-<tipo> <tipo>` (abre o navegador para entrar),
   cria a pasta local (padrão `~/Nuvem`) e, com "Sincronizar em segundo plano", põe um `rclone mount` no autostart do usuário.
4. **Emma**: liga/desliga a Emma **no sistema inteiro**; "Oi Emma" (microfone em segundo plano); provedor de IA com "Testar conexão" (pelo serviço da Emma),
   gravado em `emma/provedores.json` (o mesmo formato do Painel > Emma > Provedores, que manda ao servidor; não passa por cima de um provedor que já existe).
5. **LUMNA Connect**: gera o código de pareamento (o do Painel), mostra o endereço, espera o celular aparecer e aplica os recursos escolhidos
   (arquivos, controle de mídia, área de transferência, avisos do celular) ao celular que parear. Mudar um recurso depois de pareado vale na hora.

Toda etapa tem **Voltar**, **Pular por agora** e **Fechar**; o que se escolhe é aplicado ao sair da etapa.

## Emma desligada no sistema

`~/.config/lumna/emma-sistema.ini` (`[emma] ativa=nao`). Desligada: o serviço `lumna-emma` não sobe (sai na hora), o chat lateral e o app não abrem,
a barra não mostra o ícone nem tenta conectar. Ao desligar, o pré-setup também para o serviço e o chat que estiverem rodando; ao ligar, sobe o serviço.
Dá para ligar de novo marcando "Ativar Emma no sistema" aqui (`lumna-setup --passo 4`).

## O que ainda não existe

- O **QR code** do esboço: o app do celular ainda não lê QR, então a etapa mostra o código e o endereço (o mesmo do Painel).
- O **medidor do nível do microfone**, o **Estilo de resposta** da Emma, os botões **Acessibilidade** e **som** do esboço: não há função por trás deles no sistema.
- **NSFW** no pré-setup: pede o PIN, então liga depois pelo ícone de perfil da barra.
- O app de **e-mail** (o endereço fica guardado) e as **sugestões de apps do perfil** na Loja (ficam gravadas em `sugestoes.ini`; a Loja ainda não as mostra).

## Testes

`sh gravity/testes/setup.sh [PASTA_DO_ORBIT]`: os serviços num HOME de mentira, com timedatectl, rclone, pkill e lumna-emma de mentira (ASAN/UBSAN),
e a janela inteira num Orbit sem tela, com o serviço do celular e o da Emma de mentira.
