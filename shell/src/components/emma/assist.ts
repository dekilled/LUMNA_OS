// Emma: o que o popup e o app compartilham para ler a resposta dela.
//
// No popup a Emma termina a resposta com os próximos passos, uma linha cada (as regras vão no
// prompt da sessão, em emma.rs):
//   [acao: Adicionar layout]          → o cartão violeta (no máximo 1)
//   [link: Ver layouts instalados]    → as linhas com ícone
//   [sugestao: Qual layout escolher?] → os chips de pergunta
// Tocar num deles manda o texto como a próxima mensagem.

export interface Reply {
  /** O texto, sem as linhas de botões (nem uma meia linha ainda chegando). */
  body: string;
  primary: string | null;
  links: string[];
  suggestions: string[];
}

const TAG = /^\s*\[\s*(a[cç][aã]o|link|sugest[aã]o)\s*:\s*([^\]\n]+?)\s*\]\s*$/i;

export function parseReply(text: string): Reply {
  const reply: Reply = { body: "", primary: null, links: [], suggestions: [] };
  const lines = text.split("\n");
  const body: string[] = [];
  lines.forEach((line, i) => {
    const m = TAG.exec(line);
    if (m) {
      const kind = m[1].toLowerCase();
      const label = m[2].trim();
      if (kind.startsWith("a")) {
        if (!reply.primary) reply.primary = label;
        else if (reply.links.length < 2) reply.links.push(label);
      } else if (kind === "link") {
        if (reply.links.length < 2) reply.links.push(label);
      } else if (reply.suggestions.length < 2) {
        reply.suggestions.push(label);
      }
      return;
    }
    // a última linha ainda chegando e com cara de botão: espera ela fechar
    if (i === lines.length - 1 && /^\s*\[[^\]]*$/.test(line)) return;
    body.push(line);
  });
  reply.body = body.join("\n").trim();
  return reply;
}

/** O ícone de um botão pela primeira palavra ("Adicionar layout" → ⊕). */
export function actionIcon(label: string, primary = false): string {
  const word = label.trim().split(/\s+/)[0]?.toLowerCase() ?? "";
  if (/^(adicion|cri|nov|instal|incluir|agend)/.test(word)) return primary ? "plus-circle" : "plus";
  if (/^(ver|list|mostr|exib)/.test(word)) return "list";
  if (/^(abr|ir)/.test(word)) return "external";
  if (/^(busc|procur|pesquis)/.test(word)) return "search";
  if (/^(toc|ouv|play|reproduz)/.test(word)) return "play";
  if (/^(lig|deslig|reinici)/.test(word)) return "power";
  if (/^(config|ajust|mud|troc|alter)/.test(word)) return "settings";
  if (/^(remov|apag|exclu|limp)/.test(word)) return "trash";
  if (/^(envi|compartilh|mand)/.test(word)) return "send";
  return primary ? "sparkle" : "forward";
}

/** Texto pra falar: sem markdown nem código. */
export function speakable(text: string): string {
  return text
    .replace(/```[\s\S]*?(```|$)/g, " ")
    .replace(/`([^`]*)`/g, "$1")
    .replace(/\[([^\]]+)\]\([^)]*\)/g, "$1")
    .replace(/^\s{0,3}(#{1,6}|>|[-*+]|\d+\.)\s+/gm, "")
    .replace(/(\*\*|__|\*|~~)/g, "")
    .replace(/\s+/g, " ")
    .trim();
}

/** Onde termina a última frase completa a partir de `from` (-1 = nenhuma ainda). */
export function sentenceEnd(text: string, from: number): number {
  let end = -1;
  const re = /[.!?…](?=\s|$)|\n/g;
  re.lastIndex = from;
  let m: RegExpExecArray | null;
  while ((m = re.exec(text))) {
    // "$" no fim do texto ainda chegando pode ser um "3." de "3.5": só conta com espaço depois
    if (m.index + 1 >= text.length && m[0] !== "\n") break;
    end = m.index + 1;
  }
  return end;
}

/** Markdown leve pra mostrar como texto (o popup não desenha markdown). */
export function plainText(text: string): string {
  return text.replace(/(\*\*|__)(.+?)\1/g, "$2").replace(/`([^`]*)`/g, "$1").replace(/^#{1,6}\s+/gm, "");
}
