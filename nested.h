/*
 * Orbit rodando dentro de outra janela (um Wayland de fora: GNOME, KDE, Hyprland...): o compositor de
 * fora fica com o Super e outros atalhos dele, e eles nunca chegam aqui. O protocolo
 * keyboard-shortcuts-inhibit pede a ele que deixe os atalhos para a nossa janela enquanto ela tem o
 * foco (o GNOME pergunta uma vez; Super+Esc devolve os atalhos a ele). Fica à parte do resto do Orbit
 * porque mistura o lado cliente do Wayland, que não convive com os cabeçalhos do servidor.
 */
#ifndef ORBIT_NESTED_H
#define ORBIT_NESTED_H

#include <stdbool.h>

struct wl_display;
struct wl_surface;

/* o display e a superfície da janela da saída, no compositor de fora; false se ele não sabe o protocolo */
bool orbit_nested_inhibit(struct wl_display *remote, struct wl_surface *surface);

/*
 * A área de transferência (só texto) entre o compositor de fora e o Orbit: o que se copia lá fora cola aqui dentro e o contrário. Sem
 * isto são duas áreas separadas. Os tipos opacos (oferta) são do lado cliente; quem usa só os passa de volta.
 */
struct orbit_nested_clip_ops {
	/* o compositor de fora tem um texto copiado (oferta) ou deixou de ter (NULL) */
	void (*selecao_de_fora)(void *oferta, void *dados);
};

/* liga a ponte; false se o compositor de fora não tem como (sem data device) */
bool orbit_nested_clip_iniciar(struct wl_display *remote, const struct orbit_nested_clip_ops *ops, void *dados);
/* pede o texto da oferta de fora e o escreve em fd (fecha o fd) */
void orbit_nested_clip_receber(void *oferta, int fd);
/* a oferta não é mais usada por ninguém aqui */
void orbit_nested_clip_soltar(void *oferta);
/* o Orbit tem um texto copiado: oferece-o ao compositor de fora. enviar(mime, fd, dados) escreve o texto em fd */
void orbit_nested_clip_publicar(const char *const *mimes, int n, void (*enviar)(const char *mime, int fd, void *dados), void *dados);
/* o Orbit deixou de ter texto copiado */
void orbit_nested_clip_limpar(void);

/*
 * A janela do Orbit em tela cheia no compositor de fora. O wlroots cria o xdg_toplevel por dentro e não o entrega; pelo ponteiro da
 * saída (o início da estrutura dele) acha-se o xdg_toplevel ao lado da superfície e pede-se a tela cheia. O formato da estrutura
 * é do wlroots: se não achar, devolve false (e a janela fica como está). tam é sizeof(struct wlr_output) de quem chama.
 */
bool orbit_nested_tela_cheia(const void *saida_wlr, unsigned long tam, struct wl_surface *surface, bool ligar);

#endif
