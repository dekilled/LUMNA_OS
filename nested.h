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

#endif
