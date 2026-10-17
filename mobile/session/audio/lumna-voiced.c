// SPDX-License-Identifier: MIT
// lumna-voiced: liga o áudio da ligação no SDM845 (QDSP6 voice).
//
// O som da chamada vai direto do modem pro fone e do microfone pro modem, dentro do DSP; mas
// alguém precisa "abrir" o PCM de voz (VoiceMMode1) pro DSP começar. Este daemon escuta o
// ModemManager no D-Bus do sistema e abre o PCM quando uma chamada fica ativa (ou discando),
// e fecha quando ela acaba.
//
// Baseado no q6voiced do postmarketOS (MIT, Stephan Gerhold e outros), trocando o tinyalsa
// (que não existe no Arch) pelo alsa-lib, e achando o dispositivo sozinho em /proc/asound/pcm.
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <alsa/asoundlib.h>
#include <dbus/dbus.h>

static char device[32];
static snd_pcm_t *tx, *rx;

// "00-06: VoiceMMode1 (*) : ..." -> "hw:0,6"
static bool find_device(void)
{
	FILE *f = fopen("/proc/asound/pcm", "r");
	char line[256];
	unsigned card, dev;
	bool found = false;

	if (!f)
		return false;
	while (fgets(line, sizeof(line), f)) {
		if (strstr(line, "VoiceMMode1") && sscanf(line, "%u-%u:", &card, &dev) == 2) {
			snprintf(device, sizeof(device), "hw:%u,%u", card, dev);
			found = true;
			break;
		}
	}
	fclose(f);
	return found;
}

static snd_pcm_t *open_pcm(snd_pcm_stream_t stream)
{
	snd_pcm_t *pcm = NULL;
	int err = snd_pcm_open(&pcm, device, stream, 0);

	if (err < 0) {
		fprintf(stderr, "abrir %s (%s): %s\n", device, stream == SND_PCM_STREAM_CAPTURE ? "tx" : "rx", snd_strerror(err));
		return NULL;
	}
	// Os parâmetros quase não importam (nenhum dado de áudio passa por aqui).
	err = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 1, 8000, 0, 20000);
	if (err >= 0)
		err = snd_pcm_prepare(pcm);
	if (err < 0)
		fprintf(stderr, "preparar %s: %s\n", device, snd_strerror(err));
	return pcm;
}

static void voice_open(void)
{
	if (tx)
		return;
	if (!device[0] && !find_device()) {
		fprintf(stderr, "não achei o VoiceMMode1 em /proc/asound/pcm (o driver q6voice carregou?)\n");
		return;
	}
	tx = open_pcm(SND_PCM_STREAM_CAPTURE);
	rx = open_pcm(SND_PCM_STREAM_PLAYBACK);
	printf("voz ligada (%s)\n", device);
	fflush(stdout);
}

static void voice_close(void)
{
	if (!tx && !rx)
		return;
	if (rx)
		snd_pcm_close(rx);
	if (tx)
		snd_pcm_close(tx);
	rx = tx = NULL;
	printf("voz desligada\n");
	fflush(stdout);
}

// MMCallState: 1 discando, 2 chamando do outro lado, 4 ativa. Alguns modems não avisam
// "discando -> ativa", então discando e chamando já contam como ativa.
static bool active(int state)
{
	return state == 1 || state == 2 || state == 4;
}

int main(int argc, char **argv)
{
	DBusError err;
	DBusConnection *conn;
	DBusMessage *msg;

	if (argc == 2)
		snprintf(device, sizeof(device), "%s", argv[1]);

	dbus_error_init(&err);
	conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
	if (!conn || dbus_error_is_set(&err)) {
		fprintf(stderr, "D-Bus: %s\n", err.message ? err.message : "sem conexão");
		return 1;
	}
	dbus_bus_add_match(conn, "type='signal',interface='org.freedesktop.ModemManager1.Call',member='StateChanged'", &err);
	dbus_connection_flush(conn);
	if (dbus_error_is_set(&err)) {
		fprintf(stderr, "D-Bus match: %s\n", err.message);
		return 1;
	}
	printf("esperando ligações (%s)\n", device[0] ? device : "dispositivo automático");
	fflush(stdout);

	while (dbus_connection_read_write(conn, -1)) {
		while ((msg = dbus_connection_pop_message(conn))) {
			int old_state, new_state;

			if (dbus_message_is_signal(msg, "org.freedesktop.ModemManager1.Call", "StateChanged") &&
			    dbus_message_get_args(msg, &err, DBUS_TYPE_INT32, &old_state, DBUS_TYPE_INT32, &new_state, DBUS_TYPE_INVALID) &&
			    old_state != new_state) {
				if (active(new_state))
					voice_open();
				else if (active(old_state))
					voice_close();
			}
			if (dbus_error_is_set(&err)) {
				fprintf(stderr, "sinal: %s\n", err.message);
				dbus_error_free(&err);
			}
			dbus_message_unref(msg);
		}
	}
	return 0;
}
