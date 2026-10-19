// O estado do LUMNA Music pra qualquer tela (o app, o cartão da central e o do bloqueio): o shell
// manda `lumna://music` quando muda (e 1x por segundo tocando); entre um e outro, o tempo anda aqui.
import { onMounted, onUnmounted, ref } from "vue";
import { artUrl, musicState, on, type MusicState, type Track } from "../../system";

export function useMusic() {
  const state = ref<MusicState | null>(null);
  let ticker = 0;
  let stop: (() => void) | null = null;
  let last = Date.now();

  function set(s: MusicState) {
    state.value = s;
    last = Date.now();
  }

  onMounted(async () => {
    musicState().then(set).catch(() => {});
    stop = await on<MusicState>("lumna://music", set);
    ticker = window.setInterval(() => {
      const s = state.value;
      if (!s?.playing) return;
      const now = Date.now();
      s.position = Math.min(s.duration || Infinity, s.position + (now - last) / 1000);
      last = now;
    }, 500);
  });
  onUnmounted(() => {
    clearInterval(ticker);
    stop?.();
  });
  return state;
}

/** 3:07, 1:02:15 */
export function clock(seconds: number): string {
  if (!Number.isFinite(seconds) || seconds < 0) seconds = 0;
  const s = Math.floor(seconds % 60);
  const m = Math.floor(seconds / 60) % 60;
  const h = Math.floor(seconds / 3600);
  const two = (n: number) => String(n).padStart(2, "0");
  return h ? `${h}:${two(m)}:${two(s)}` : `${m}:${two(s)}`;
}

/** A capa: arquivo do celular (pelo protocolo asset) ou endereço da internet (Qobuz). */
export const coverUrl = (t: Track | null | undefined) => (t?.cover ? (/^https?:/.test(t.cover) ? t.cover : artUrl(t.cover)) : null);
export const isQobuz = (t: Track) => t.path.startsWith("qobuz:");

/** "Coldplay" (ou o artista do álbum; sem tag: "Artista desconhecido"). */
export const artistOf = (t: Track) => t.album_artist || t.artist || "Artista desconhecido";
export const albumOf = (t: Track) => t.album || "Sem álbum";
