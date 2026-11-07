#!/bin/sh
# O polkit-agent-helper-1 de mentira (modo programa): $1 é a conta, o cookie vem na entrada; a senha certa é "certa".
read -r cookie
echo "$1 $cookie" >> "$JANELA_DIR/helper.log"
echo "PAM_PROMPT_ECHO_OFF Password: "
read -r senha
if [ "$senha" = certa ]; then echo SUCCESS; else echo FAILURE; fi
