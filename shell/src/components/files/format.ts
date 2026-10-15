import type { FileKind } from "../../system";

export const KIND_LABEL: Record<FileKind, string> = {
  folder: "Pasta",
  image: "Imagem",
  video: "Vídeo",
  audio: "Música",
  pdf: "PDF",
  document: "Documento",
  sheet: "Planilha",
  slides: "Apresentação",
  archive: "Arquivo compactado",
  code: "Código",
  text: "Texto",
  app: "Programa",
  disk: "Imagem de disco",
  file: "Arquivo",
};

/** Ícone de cada tipo (nomes do Icon.vue). */
export const KIND_ICON: Record<FileKind, string> = {
  folder: "folder",
  image: "image",
  video: "video",
  audio: "music",
  pdf: "pdf",
  document: "document",
  sheet: "sheet",
  slides: "slides",
  archive: "archive",
  code: "code",
  text: "text",
  app: "app",
  disk: "drive",
  file: "file",
};

export function formatSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  const units = ["KB", "MB", "GB", "TB"];
  let value = bytes / 1024;
  let unit = 0;
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024;
    unit++;
  }
  return `${value >= 100 ? Math.round(value) : value.toFixed(1).replace(".", ",")} ${units[unit]}`;
}

const time = new Intl.DateTimeFormat("pt-BR", { hour: "2-digit", minute: "2-digit" });
const date = new Intl.DateTimeFormat("pt-BR", { day: "numeric", month: "short", year: "numeric" });

/** "Hoje 14:32", "Ontem 09:10" ou "12 de out. de 2026". */
export function formatDate(seconds: number): string {
  if (!seconds) return "-";
  const d = new Date(seconds * 1000);
  const today = new Date();
  const yesterday = new Date(today.getTime() - 86_400_000);
  const same = (a: Date, b: Date) => a.toDateString() === b.toDateString();
  if (same(d, today)) return `Hoje ${time.format(d)}`;
  if (same(d, yesterday)) return `Ontem ${time.format(d)}`;
  return date.format(d);
}

/** "/home/lumna/Documentos" → ["/", "home", "lumna", "Documentos"] com o caminho de cada um. */
export function crumbs(path: string, home: string): { name: string; path: string; home?: boolean }[] {
  const list: { name: string; path: string; home?: boolean }[] = [];
  let rest = path;
  if (home && (path === home || path.startsWith(home + "/"))) {
    list.push({ name: "Início", path: home, home: true });
    rest = path.slice(home.length);
  } else {
    list.push({ name: "Computador", path: "/" });
  }
  let acc = list[0].path === "/" ? "" : list[0].path;
  for (const part of rest.split("/").filter(Boolean)) {
    acc += "/" + part;
    list.push({ name: part, path: acc });
  }
  return list;
}
