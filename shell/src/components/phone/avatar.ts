// Bolinha do contato: a inicial e uma cor fixa pra cada nome.
const COLORS = ["#4c3a8f", "#3a4a86", "#6b3a6e", "#33506b", "#5a3f8c", "#3f5a7a", "#6e4060"];

export function initial(name: string): string {
  const ch = name.trim().charAt(0).toUpperCase();
  // números e símbolos ficam juntos em "#"
  return /\p{L}/u.test(ch) ? ch.normalize("NFD").charAt(0) : "#";
}

export function avatarColor(name: string): string {
  let hash = 0;
  for (const ch of name) hash = (hash * 31 + ch.charCodeAt(0)) >>> 0;
  return COLORS[hash % COLORS.length];
}
