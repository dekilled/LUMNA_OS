#include "hora.h"
#include <assert.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
	struct tm t = { .tm_hour = 14, .tm_min = 5, .tm_sec = 9 };
	char b[32];
	/* sem hora.ini: 24 horas */
	hora_texto(&t, false, b, sizeof(b));
	assert(hora_24h() && !strcmp(b, "14:05"));
	hora_texto(&t, true, b, sizeof(b));
	assert(!strcmp(b, "14:05:09"));
	/* o Painel grava o formato 12: a leitura vale em até 2 segundos */
	assert(g_file_set_contents(argv[1], "[geral]\nformato = 12\n", -1, NULL));
	sleep(3);
	assert(!hora_24h());
	hora_texto(&t, false, b, sizeof(b));
	assert(!strcmp(b, "2:05 PM"));
	t.tm_hour = 0;
	hora_texto(&t, true, b, sizeof(b));
	assert(!strcmp(b, "12:05:09 AM"));
	t.tm_hour = 12;
	hora_texto(&t, false, b, sizeof(b));
	assert(!strcmp(b, "12:05 PM"));
	puts("hora ok");
	return 0;
}
