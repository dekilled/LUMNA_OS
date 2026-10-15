import type { StoreSource } from "../../system";

/** Programa escolhido a dedo para a aba Descobrir, já com a fonte certa para o LUMNA. */
export interface Pick {
  source: StoreSource;
  name: string;
  repo: string;
  title: string;
  summary: string;
  /** Ícone do Flathub (também para os que vêm do Arch ou do AUR, que não têm ícone próprio). */
  flathubId: string;
}

const icon = (id: string) => `https://dl.flathub.org/repo/appstream/x86_64/icons/128x128/${id}.png`;
export const iconFor = (p: Pick) => icon(p.flathubId);

export const SHELVES: { title: string; picks: Pick[] }[] = [
  {
    title: "Para jogar",
    picks: [
      { source: "arch", name: "steam", repo: "multilib", title: "Steam", summary: "A maior loja de jogos do PC, com o Proton para os jogos de Windows", flathubId: "com.valvesoftware.Steam" },
      { source: "flathub", name: "com.heroicgameslauncher.hgl", repo: "flathub", title: "Heroic", summary: "Jogos da Epic, GOG e Amazon", flathubId: "com.heroicgameslauncher.hgl" },
      { source: "arch", name: "lutris", repo: "extra", title: "Lutris", summary: "Jogos de outras lojas e emuladores num lugar só", flathubId: "net.lutris.Lutris" },
      { source: "flathub", name: "net.davidotek.pupgui2", repo: "flathub", title: "ProtonUp-Qt", summary: "Versões novas do Proton para a Steam e o Heroic", flathubId: "net.davidotek.pupgui2" },
      { source: "flathub", name: "com.discordapp.Discord", repo: "flathub", title: "Discord", summary: "Voz e chat com os amigos", flathubId: "com.discordapp.Discord" },
    ],
  },
  {
    title: "Música e vídeo",
    picks: [
      { source: "flathub", name: "com.spotify.Client", repo: "flathub", title: "Spotify", summary: "Música e podcasts", flathubId: "com.spotify.Client" },
      { source: "arch", name: "vlc", repo: "extra", title: "VLC", summary: "Toca qualquer vídeo ou música", flathubId: "org.videolan.VLC" },
      { source: "flathub", name: "com.obsproject.Studio", repo: "flathub", title: "OBS Studio", summary: "Gravar a tela e fazer live", flathubId: "com.obsproject.Studio" },
      { source: "flathub", name: "org.kde.kdenlive", repo: "flathub", title: "Kdenlive", summary: "Editor de vídeo", flathubId: "org.kde.kdenlive" },
    ],
  },
  {
    title: "Trabalho e criação",
    picks: [
      { source: "arch", name: "libreoffice-fresh", repo: "extra", title: "LibreOffice", summary: "Textos, planilhas e apresentações", flathubId: "org.libreoffice.LibreOffice" },
      { source: "flathub", name: "org.gimp.GIMP", repo: "flathub", title: "GIMP", summary: "Edição de imagens", flathubId: "org.gimp.GIMP" },
      { source: "flathub", name: "org.inkscape.Inkscape", repo: "flathub", title: "Inkscape", summary: "Desenho vetorial", flathubId: "org.inkscape.Inkscape" },
      { source: "aur", name: "visual-studio-code-bin", repo: "visual-studio-code-bin", title: "Visual Studio Code", summary: "Editor de código da Microsoft", flathubId: "com.visualstudio.code" },
      { source: "flathub", name: "org.telegram.desktop", repo: "flathub", title: "Telegram", summary: "Mensagens", flathubId: "org.telegram.desktop" },
    ],
  },
  {
    title: "Navegadores",
    picks: [
      { source: "arch", name: "chromium", repo: "extra", title: "Chromium", summary: "O navegador aberto do Google", flathubId: "org.chromium.Chromium" },
      { source: "flathub", name: "com.brave.Browser", repo: "flathub", title: "Brave", summary: "Navegador com bloqueio de anúncios", flathubId: "com.brave.Browser" },
      { source: "aur", name: "google-chrome", repo: "google-chrome", title: "Google Chrome", summary: "O navegador do Google", flathubId: "com.google.Chrome" },
    ],
  },
];

export const SOURCE_LABEL: Record<StoreSource, string> = { arch: "Arch", flathub: "Flathub", aur: "AUR" };
