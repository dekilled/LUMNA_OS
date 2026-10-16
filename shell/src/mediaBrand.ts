// Cores de cada serviço da Mídia (só o nome e uma cor; nada de logotipos).

export const SERVICE_COLOR: Record<string, string> = {
  youtube: "#ff3d3d",
  netflix: "#e50914",
  prime: "#38a8ff",
  disney: "#6fa3ff",
  paramount: "#2f7bff",
  music: "#c084fc",
};

/** Fundo dos cartões de serviço. */
export const SERVICE_STYLE: Record<string, string> = {
  youtube: "linear-gradient(135deg, #3a0a0a, #120303)",
  netflix: "linear-gradient(135deg, #3a0507, #0b0102)",
  prime: "linear-gradient(135deg, #0b2a4a, #04101e)",
  disney: "linear-gradient(135deg, #0c1a4d, #050a20)",
  paramount: "linear-gradient(135deg, #0a2463, #030b22)",
  music: "linear-gradient(135deg, #3d1466, #12061f)",
};

/** Marca curta no selo do serviço (a inicial). */
export const serviceMark = (name: string) => name.replace(/[^A-Za-zÀ-ú]/g, "").charAt(0).toUpperCase();
