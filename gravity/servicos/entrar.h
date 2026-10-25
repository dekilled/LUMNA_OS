/*
 * Conferir quem desbloqueia: a senha do Linux (pelo unix_chkpwd, o ajudante do próprio PAM, que
 * confere a de quem chama) ou o PIN do LUMNA (4 a 8 números, em ~/.config/lumna/mobile-pin como
 * "tamanho:sal:hash", o mesmo arquivo e a mesma conta do shell antigo do celular: um PIN feito lá
 * vale aqui). Errou 5 vezes: espera 30 s, e o dobro a cada nova rodada (até 10 min).
 */
#ifndef LUMNA_ENTRAR_H
#define LUMNA_ENTRAR_H

#include <stdbool.h>

/* o tamanho do PIN guardado (0: não há PIN) */
int entrar_pin_tamanho(void);
bool entrar_pin_confere(const char *pin);
bool entrar_senha_confere(const char *senha);
/* grava um PIN novo (precisa da senha do Linux); devolve NULL ou o erro */
const char *entrar_pin_definir(const char *senha, const char *pin);
/* tira o PIN (precisa da senha) */
const char *entrar_pin_tirar(const char *senha);

/* as tentativas: chame entrar_errou a cada erro; enquanto entrar_espera() > 0, não deixe tentar */
void entrar_errou(void);
void entrar_acertou(void);
int entrar_espera(void); /* segundos que faltam */

#endif
