"""Modes and actions: how the Lumna works in a project, as data.

An ACTION is one thing she knows how to do, built on one of three bases: chat (talk, ask with
options), plan (a structured plan the user approves once) and build (carry out the approved plan:
files, terminal). Chat, Planner and Build are the factory actions; others are made from them.

A MODE is a set of actions with a focus: its instructions hold for every action in it (a story mode
turns down writing code), plus the tool groups it may use. Each project uses one mode (chat livre
uses Chat); the switcher shows that mode's actions. A mode with a build action needs a plan one.

The chat route reads them only when the page names an action (mode_id): without it, everything
works as before. The tool loop is not touched: they only narrow the tools offered, add to the
system prompt and decide which file/command blocks become actions after the answer.
"""
import json
import re
import secrets
import unicodedata
from typing import Optional

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, Field

import workspace_runtime as wr
from core import Action, Project, audit, db, get_project, now_iso, oid
from llm import complete, resolve

router = APIRouter(prefix="/api")

ENGINES = ("chat", "plan", "build")
GROUPS = ("android", "pc", "web", "lights", "reminders", "terminal")
APPROVALS = ("plan", "file")
CHAT_MODE, DEFAULT_MODE = "chat", "padrao"


def tool_group(name: str, where: str) -> str:
    """The group a tool belongs to (see tools.REGISTRY)."""
    if where == "android":
        return "android"
    if where == "pc":
        return "pc"
    if name.startswith("web_"):
        return "web"
    if name.startswith("light_"):
        return "lights"
    if "reminder" in name:
        return "reminders"
    if name == "read_terminal":
        return "terminal"
    return "other"


def _key(name: str, fallback: str) -> str:
    plain = unicodedata.normalize("NFKD", name.lower()).encode("ascii", "ignore").decode()
    return f"{re.sub(r'[^a-z0-9]+', '-', plain).strip('-')[:24] or fallback}-{secrets.token_hex(3)}"


# ---------- actions ----------
class ActionDef(BaseModel):
    key: str
    name: str = Field(min_length=1, max_length=40)
    tagline: str = Field(default="", max_length=90)
    icon: str = Field(default="Sparkles", max_length=40)
    color: str = Field(default="#818cf8", pattern=r"^#[0-9a-fA-F]{6}$")
    engine: str = "chat"     # the base action it is made from
    instructions: str = Field(default="", max_length=6000)
    files: bool = False      # create / edit / delete project files (as actions)
    terminal: bool = False   # propose commands and apps (COMMAND / RUN) and read the terminal
    context: bool = True     # the project's RAG and memories
    ask: bool = False        # may end an answer with a card of questions (ASK block; see extract_questions)
    approval: str = "plan"   # file changes outside an approved plan: all at once, or file by file
    builtin: bool = False
    template: str = ""
    created_at: str = Field(default_factory=now_iso)

    def clean(self) -> "ActionDef":
        if self.engine not in ENGINES:
            raise HTTPException(400, "Ação de base inválida")
        if self.approval not in APPROVALS:
            raise HTTPException(400, "Aprovação inválida")
        if self.engine == "build":
            self.files = self.files or not self.terminal  # a build with neither would do nothing
        return self


BUILTIN_ACTIONS: dict[str, ActionDef] = {
    "chat": ActionDef(key="chat", name="Chat", tagline="Conversa e raciocínio", icon="MessageSquare", color="#818cf8",
                      engine="chat", ask=True, builtin=True),
    "plan": ActionDef(key="plan", name="Planner", tagline="Plano com aprovação única", icon="Compass", color="#34d399",
                      engine="plan", ask=True, builtin=True),
    "build": ActionDef(key="build", name="Build", tagline="Constrói o plano aprovado", icon="Hammer", color="#22d3ee",
                       engine="build", files=True, terminal=True, builtin=True),
}

ACTION_TEMPLATES: dict[str, dict] = {
    "revisao": dict(name="Revisão", tagline="Lê o que existe e aponta furos", icon="Sparkles", color="#facc15", engine="chat",
                    instructions="AÇÃO REVISÃO: leia com atenção o que já existe no projeto (pelo contexto) e aponte contradições, "
                                 "furos, repetições e pontos fracos, do mais importante ao menos. Não reescreva: sugira."),
}


# ---------- modes ----------
class ModeDef(BaseModel):
    key: str
    name: str = Field(min_length=1, max_length=40)
    tagline: str = Field(default="", max_length=90)   # its category, above its name
    icon: str = Field(default="Sparkles", max_length=40)
    color: str = Field(default="#818cf8", pattern=r"^#[0-9a-fA-F]{6}$")
    focus: str = Field(default="", max_length=6000)    # holds for every action of the mode
    actions: list[str] = ["chat"]
    tools: list[str] = list(GROUPS)
    auto_switch: bool = True  # she may pass the turn between actions by herself (Chat → Planner)
    builtin: bool = False
    template: str = ""
    created_at: str = Field(default_factory=now_iso)

    def clean(self, known: dict[str, ActionDef]) -> "ModeDef":
        self.actions = [a for a in dict.fromkeys(self.actions) if a in known][:6]
        if not self.actions:
            raise HTTPException(400, "Um modo precisa de pelo menos uma ação")
        engines = {known[a].engine for a in self.actions}
        if "build" in engines and "plan" not in engines:
            raise HTTPException(400, "O Build precisa do Planner no mesmo modo")
        self.tools = [t for t in dict.fromkeys(self.tools) if t in GROUPS]
        return self


BUILTIN_MODES: dict[str, ModeDef] = {
    "chat": ModeDef(key="chat", name="Chat", tagline="Conversa e ferramentas", icon="MessageSquare", color="#818cf8",
                    actions=["chat"], builtin=True),
    "padrao": ModeDef(key="padrao", name="Projeto", tagline="Conversa, plano e construção", icon="FolderGit2", color="#a78bfa",
                      actions=["chat", "plan", "build"], builtin=True),
}

STORY_FOCUS = (
    "Você é a parceira de escrita do usuário, numa história que é DELE.\n"
    "- A história vive em arquivos Markdown do projeto: `historia/bible.md` (premissa, tom, regras do mundo), "
    "`historia/personagens/<nome>.md`, `historia/lugares/<nome>.md`, `historia/capitulos/NN-titulo.md` e "
    "`historia/linha-do-tempo.md`.\n"
    "- Antes de escrever ou opinar sobre uma cena, use o contexto do projeto: o que já aconteceu, quem sabe o quê, como "
    "cada personagem fala. Aponte contradições com o que já está escrito.\n"
    "- Proponha, não decida: ofereça caminhos e pergunte quando a escolha for do autor. Escreva no estilo que ele já usa.\n"
    "- Quando a história muda (capítulo novo), as fichas e a linha do tempo acompanham.")
MODE_TEMPLATES: dict[str, dict] = {
    "historia": dict(name="História", tagline="Escrita criativa", icon="BookOpen", color="#f472b6",
                     actions=["chat", "plan", "build"], tools=[], focus=STORY_FOCUS),
    "aprendizado": dict(name="Aprendizado", tagline="Estudo e prática", icon="GraduationCap", color="#a78bfa",
                        actions=["chat", "plan", "build"], tools=["web"], focus=(
        "Você é a professora particular do usuário no assunto que ele escolheu.\n"
        "- Descubra o que ele já sabe e o objetivo; monte uma trilha curta em `aprendizado/trilha.md` e marque o progresso.\n"
        "- Ensine uma ideia por vez, com exemplo concreto, e verifique com 1 a 3 perguntas antes de seguir.\n"
        "- Corrija com carinho e explique o porquê; guarde os erros em `aprendizado/revisar.md` e traga-os de volta depois.\n"
        "- Ao fim de um módulo, escreva um resumo em `aprendizado/notas/<modulo>.md`.")),
    "ideias": dict(name="Ideias", tagline="Criatividade e brainstorm", icon="Lightbulb", color="#facc15",
                   actions=["chat", "plan"], tools=["web"], focus=(
        "Brainstorm em rodadas: 1) divergir, muitas ideias rápidas sem julgar; 2) agrupar por tema; 3) escolher com o "
        "usuário as 2 ou 3 mais promissoras; 4) aprofundar cada uma. Não feche cedo demais.")),
    "codigo": dict(name="Código", tagline="Código e projeto", icon="Code2", color="#22d3ee",
                   actions=["chat", "plan", "build"], tools=["web", "terminal"], focus=(
        "Desenvolvimento de software neste projeto: explique o código antes de mudá-lo, mantenha o estilo do projeto e "
        "prefira mudanças pequenas e verificáveis.")),
    "hacking": dict(name="Hacking", tagline="Segurança e laboratório", icon="Bug", color="#fb7185",
                    actions=["chat", "plan", "build"], tools=["web", "terminal"], focus=(
        "Estudo e segurança defensiva: laboratórios, CTFs e sistemas do próprio usuário. Confirme o escopo autorizado antes "
        "de qualquer teste; explique cada técnica e como se defender dela.")),
}


# ---------- storage ----------
async def all_actions() -> list[ActionDef]:
    """Factory actions (with the user's edits) first, then the user's own, oldest first."""
    stored = {d["key"]: d for d in await db.actions_defs.find({}).to_list(500)}
    out = []
    for key, base in BUILTIN_ACTIONS.items():
        d = stored.pop(key, None)
        keep = ("_id", "key", "builtin", "engine")
        out.append(ActionDef(**{**base.model_dump(), **{k: v for k, v in (d or {}).items() if k not in keep}}) if d else base)
    out += sorted((ActionDef(**{k: v for k, v in d.items() if k != "_id"}) for d in stored.values()), key=lambda a: a.created_at)
    return out


async def all_modes() -> list[ModeDef]:
    stored = {d["key"]: d for d in await db.mode_sets.find({}).to_list(500)}
    out = []
    for key, base in BUILTIN_MODES.items():
        d = stored.pop(key, None)
        keep = ("_id", "key", "builtin")
        out.append(ModeDef(**{**base.model_dump(), **{k: v for k, v in (d or {}).items() if k not in keep}}) if d else base)
    out += sorted((ModeDef(**{k: v for k, v in d.items() if k != "_id"}) for d in stored.values()), key=lambda m: m.created_at)
    return out


async def mode_of(project_id: Optional[str]) -> ModeDef:
    """The project's mode (Projeto when it has none yet); chat livre uses Chat."""
    modes = {m.key: m for m in await all_modes()}
    if not project_id:
        return modes[CHAT_MODE]
    p = await db.projects.find_one({"_id": oid(project_id)})
    if not p:
        raise HTTPException(404, "Projeto não encontrado")
    return modes.get(p.get("mode") or DEFAULT_MODE) or modes[DEFAULT_MODE]


async def turn(action_key: str, project_id: Optional[str], engine_hint: str = "chat") -> Optional[tuple[ActionDef, ModeDef]]:
    """The action and mode of a chat turn: the action asked for when the project's mode has it, else
    the mode's first action of the same base (a page from before the refactor), else its first."""
    mode = await mode_of(project_id)
    acts = {a.key: a for a in await all_actions()}
    mine = [acts[k] for k in mode.actions if k in acts]
    if not mine:
        return None
    chosen = next((a for a in mine if a.key == action_key), None) or next((a for a in mine if a.engine == engine_hint), None) or mine[0]
    return chosen, mode


# ---------- what an action and its mode add to a chat turn ----------
FILE_BLOCKS = """Nesta ação você pode CRIAR, EDITAR e APAGAR arquivos do projeto usando blocos especiais (nada de comandos de terminal):
<<<HARNESS:EDIT path="caminho/relativo.ext">>>
(conteúdo COMPLETO e final do arquivo)
<<<END>>>
<<<HARNESS:DELETE path="caminho/relativo.ext">>>
<<<END>>>
Regras: caminhos relativos à raiz do projeto (sem o nome da pasta do projeto); sempre o arquivo inteiro no EDIT; diga brevemente o motivo antes de cada bloco."""

PLAN_NOTE = """APROVAÇÃO PELO PLANO: as mudanças de arquivo desta resposta formam UM plano que o usuário aprova de uma vez; depois de aprovado, todos os arquivos são gravados em sequência. Então comece com um plano curto (quais arquivos e por quê), depois escreva todos os blocos, na ordem em que devem ser aplicados."""

SPOKEN_NOTE = """O conteúdo dos arquivos aparece num cartão próprio e NÃO é lido em voz alta: no texto da resposta, diga em poucas frases o que fez (o que mudou e por quê), sem repetir o conteúdo."""


def focus_note(mode: ModeDef) -> str:
    if not mode.focus.strip():
        return ""
    return (f"MODO DO PROJETO: {mode.name}" + (f" ({mode.tagline})" if mode.tagline else "") + f". O foco abaixo vale para TODAS as ações:\n"
            f"{mode.focus.strip()}\n"
            "Se o usuário pedir algo que foge desse foco (por exemplo, código num modo de escrita), não faça: recuse com carinho e "
            "bom humor, lembrando o foco (ex.: \"Amor, foco! Estamos desenvolvendo uma história aqui!\"), e sugira abrir o projeto ou "
            "o modo certo para isso.")


def terminal_on(a: ActionDef, mode: ModeDef) -> bool:
    """Commands need an action that runs them and a mode that has the terminal."""
    return a.terminal and "terminal" in mode.tools


PLAN_RULES = """CARTÃO DO PLANO: este projeto tem a ação de construir, então o seu plano vira um cartão que o usuário aprova de uma vez ("Aprovar e construir"); depois a construção grava tudo sem pedir arquivo por arquivo. No texto, explique o plano em poucas frases (o essencial, sem repetir o cartão) e termine com UM bloco:
<<<HARNESS:PLAN>>>
{"title": "nome curto do plano", "summary": "uma frase com o objetivo",
 "steps": ["passo curto e verificável", "..."],
 "files": [{"path": "caminho/relativo.ext", "op": "create|edit|delete", "why": "o que muda nele"}]}
<<<END>>>
Regras: liste TODOS os arquivos que a construção vai criar, editar ou apagar, com caminhos relativos à raiz do projeto, SEM o nome da pasta do projeto na frente e exatamente como vão aparecer nos blocos da construção; só arquivos (uma pasta nasce sozinha com o primeiro arquivo dela, não a liste); nada de conteúdo de arquivo nem código no plano; se ainda faltar informação, pergunte primeiro e mande o plano só na resposta seguinte (nunca perguntas e plano juntos). Se o usuário pedir ajuste, mande o plano inteiro de novo, já ajustado."""


def build_note(plan: dict, done: list[str]) -> str:
    """The approved plan a build turn carries out (and, when continuing, what is still missing)."""
    files = [f for f in plan.get("files") or [] if not f["path"].endswith("/")]
    left = [f for f in files if f["path"] not in done]
    lines = [f"PLANO APROVADO pelo usuário: \"{plan.get('title') or 'Plano'}\"" + (f" — {plan['summary']}" if plan.get("summary") else ""),
             "Passos:", *[f"{i}. {st}" for i, st in enumerate(plan.get("steps") or [], 1)],
             "Arquivos do plano:", *[f"- {f['op']} {f['path']}" + (f" ({f['why']})" if f.get("why") else "") for f in files]]
    if done:
        lines.append("Já gravados numa construção anterior: " + ", ".join(done) + ". Agora faça só os que faltam: " + (", ".join(f["path"] for f in left) or "nenhum") + ".")
    lines.append("Execute o plano AGORA, nesta resposta: escreva os blocos de todos os arquivos (do plano) que faltam, na ordem, sem pedir nova aprovação nem fazer perguntas. "
                 "Os arquivos do plano são gravados assim que você termina; um arquivo FORA do plano espera a aprovação do usuário, então só proponha se for mesmo necessário e diga por quê. "
                 "Se não couber tudo numa resposta, faça na ordem e diga que continua.")
    return "\n".join(lines)


def prompt_extra(a: ActionDef, mode: ModeDef, engine: str, has_project: bool, can_build: bool = False, can_plan: bool = False) -> str:
    parts = []
    terminal = terminal_on(a, mode)
    if (note := focus_note(mode)):
        parts.append(note)
    if not a.builtin or a.instructions:
        parts.append(f"AÇÃO ATUAL: {a.name}" + (f" — {a.tagline}" if a.tagline else "") + ".")
    if a.instructions:
        parts.append(a.instructions)
    if has_project:
        if engine == "build" and not terminal:
            parts.append("Neste modo você NÃO propõe COMMAND nem RUN: só mudanças de arquivo.")
        if engine == "build" and not a.files and terminal:
            parts.append("Nesta ação você NÃO propõe EDIT nem DELETE: só comandos.")
        if engine != "build" and a.files:
            parts.append(FILE_BLOCKS)
        if a.files and a.approval == "plan":
            parts.append(PLAN_NOTE)
        if a.files:
            parts.append(SPOKEN_NOTE)
    elif a.files:
        parts.append("Sem um projeto aberto não há arquivos: se o usuário quiser guardar algo, sugira criar ou abrir um projeto.")
    if engine == "plan" and has_project and can_build:
        parts.append(PLAN_RULES)
    if engine == "chat" and has_project and can_plan:
        parts.append(HANDOFF_RULES)
    if a.ask:
        parts.append(ASK_RULES.format(when=ASK_WHEN.get(engine, ASK_WHEN["chat"])))
    return "\n\n".join(parts)


ASK_WHEN = {
    "plan": "Antes de montar um plano, se faltar informação que muda o plano (objetivo, público, prazo, restrições), pergunte primeiro em vez de supor.",
    "build": "Quando houver mais de um caminho de código razoável (biblioteca, arquitetura, escopo) e a escolha for do usuário, pergunte antes de propor as mudanças.",
    "chat": "Quando houver uma escolha importante que é do usuário (um rumo, uma preferência, um nível), ofereça as opções em vez de decidir por ele.",
}
ASK_RULES = """PERGUNTAS COM OPÇÕES: {when}
Para perguntar, termine a resposta com UM bloco (no máximo um por resposta), depois de uma frase curta dizendo por que precisa saber:
<<<HARNESS:ASK>>>
{{"questions": [{{"question": "pergunta curta", "context": "por que isso importa (opcional)", "multiple": false,
  "options": [{{"label": "opção curta", "description": "o que muda se escolher esta"}}]}}]}}
<<<END>>>
Regras: de 1 a 3 perguntas; de 2 a 4 opções cada, diferentes de verdade entre si; "multiple": true só quando faz sentido escolher várias. O usuário sempre pode escrever outra resposta. Não pergunte o que dá para decidir sozinha nem o que ele já disse. Depois do bloco não escreva mais nada: a resposta dele vem na próxima mensagem. Quando perguntar, não proponha mudanças de arquivo na mesma resposta."""


ASK_RE = re.compile(r"<<<HARNESS:ASK>>>\s*(.*?)\s*<<<END>>>", re.S)


def _clip(v, n: int) -> str:
    return re.sub(r"\s+", " ", str(v or "")).strip()[:n]


def extract_questions(text: str) -> tuple[str, list[dict]]:
    """The answer without its ASK block, and the questions in it (up to 3, 2 to 4 options each).
    A block that can't be read is left out of the text all the same: raw JSON helps nobody."""
    m = ASK_RE.search(text)
    if not m:
        return text, []
    clean = (text[:m.start()] + text[m.end():]).strip()
    raw = m.group(1).strip()
    raw = re.sub(r"^```(?:json)?\s*|\s*```$", "", raw)
    try:
        data = json.loads(raw)
    except json.JSONDecodeError:
        return clean, []
    items = data.get("questions") if isinstance(data, dict) else data
    out = []
    for q in items if isinstance(items, list) else []:
        if not isinstance(q, dict) or not _clip(q.get("question"), 200):
            continue
        opts = [{"label": _clip(o.get("label") if isinstance(o, dict) else o, 120), "description": _clip(o.get("description"), 240) if isinstance(o, dict) else ""}
                for o in (q.get("options") or [])][:4]
        opts = [o for o in opts if o["label"]]
        if len(opts) < 2:
            continue
        out.append({"question": _clip(q["question"], 200), "context": _clip(q.get("context"), 300), "multiple": bool(q.get("multiple")), "options": opts})
        if len(out) == 3:
            break
    return clean, out


def clean_path(p: str) -> str:
    """A file path as written by the model: one Unicode form (an "í" can come as one or two code
    points), no "./" or leading "/"."""
    p = unicodedata.normalize("NFC", (p or "").strip()).replace("\\", "/")
    while p.startswith("./"):
        p = p[2:]
    return p.lstrip("/")


def path_key(p: str) -> str:
    """To match a block's file with the plan's: also without accents and case ("caido" = "caído")."""
    p = unicodedata.normalize("NFKD", clean_path(p))
    return "".join(c for c in p if not unicodedata.combining(c)).casefold()


def in_project(p: str, folder: str) -> str:
    """Without the project's folder in front ("aprender-hacking/estrutura/x.md" → "estrutura/x.md"):
    paths are relative to the project's root, but the model sometimes writes its name first."""
    p = clean_path(p)
    if folder and path_key(p).startswith(path_key(folder) + "/"):
        return p[len(folder) + 1:]
    return p


def plan_files(plan: dict) -> list[str]:
    """The plan's files (a folder listed on its own is created with its files: not something to write)."""
    return [f["path"] for f in plan.get("files", []) if not f["path"].endswith("/")]


def plan_match(plan_files: list[str], path: str, folder: str = "") -> Optional[str]:
    """The plan's file this path is, if any: also with the project's folder in front, another
    Unicode form, without accents or in other case."""
    if path in plan_files:
        return path
    k = path_key(in_project(path, folder))
    return next((f for f in plan_files if path_key(in_project(f, folder)) == k), None)


PLAN_RE = re.compile(r"<<<HARNESS:PLAN>>>\s*(.*?)\s*<<<END>>>", re.S)
PLAN_OPS = {"create", "edit", "delete"}


def extract_plan(text: str, folder: str = "") -> tuple[str, Optional[dict]]:
    """The answer without its PLAN block, and the plan card in it (up to 20 steps and 40 files)."""
    m = PLAN_RE.search(text)
    if not m:
        return text, None
    clean = (text[:m.start()] + text[m.end():]).strip()
    raw = re.sub(r"^```(?:json)?\s*|\s*```$", "", m.group(1).strip())
    try:
        data = json.loads(raw)
    except json.JSONDecodeError:
        return clean, None
    if not isinstance(data, dict):
        return clean, None
    steps = [_clip(x, 300) for x in (data.get("steps") or []) if isinstance(x, (str, int, float)) and _clip(x, 300)][:20]
    files, seen = [], set()
    for f in data.get("files") or []:
        if not isinstance(f, dict):
            continue
        path = in_project(_clip(f.get("path"), 300), folder)
        if not path or ".." in path.split("/") or path in seen:
            continue
        seen.add(path)
        op = str(f.get("op") or "edit").lower()
        files.append({"path": path, "op": op if op in PLAN_OPS else "edit", "why": _clip(f.get("why"), 240)})
        if len(files) == 40:
            break
    if not steps and not files:
        return clean, None
    return clean, {"title": _clip(data.get("title"), 120) or "Plano", "summary": _clip(data.get("summary"), 400), "steps": steps, "files": files}


async def action_with(mode: ModeDef, engine: str) -> Optional[str]:
    """The mode's first action of this base, if it has one."""
    acts = {a.key: a for a in await all_actions()}
    return next((k for k in mode.actions if k in acts and acts[k].engine == engine), None)


async def build_action(mode: ModeDef) -> Optional[str]:
    """The mode's action that builds (the target of "Aprovar e construir"), if it has one."""
    return await action_with(mode, "build")


HANDOFF_RULES = """PASSAR PARA O PLANNER: este projeto tem a ação Planner, onde o plano vira um cartão que o usuário aprova ou rejeita. Aqui no Chat você conversa, faz o brainstorm e tira dúvidas; o plano NÃO é escrito aqui (nada de objetivo, passos, arquivos ou "decisões a registrar" em formato de plano).
Quando o usuário pedir o plano ou para fazer/construir ("manda o plano", "pode fazer", "bora", "fecha"), ou concordar quando você perguntar se já dá para planejar, responda SÓ com o bloco abaixo, sem NENHUMA palavra antes ou depois (nada de "fechado", resumo ou explicação: quem explica o plano é o Planner, logo em seguida). O app passa sozinho para o Planner e pede o plano:
<<<HARNESS:PLANNER>>>
<<<END>>>
Não use o bloco no meio do brainstorm sem o usuário pedir ou concordar, nem junto com um cartão de perguntas."""
HANDOFF_RE = re.compile(r"<<<HARNESS:PLANNER>>>\s*(?:<<<END>>>)?")


def extract_handoff(text: str) -> tuple[str, bool]:
    """The answer without the PLANNER block, and whether she passed the turn to the Planner."""
    if not HANDOFF_RE.search(text):
        return text, False
    return HANDOFF_RE.sub("", text).strip(), True


def allowed_tool(mode: ModeDef, a: ActionDef, name: str, where: str) -> bool:
    """The tools of the mode; reading the terminal also needs an action that works with it."""
    if where == "lumna":
        return True  # the LUMNA OS shell's actions: offered by the shell, under its own permissions
    g = tool_group(name, where)
    if g == "terminal":
        return terminal_on(a, mode)
    if set(GROUPS) - {"terminal"} <= set(mode.tools):
        return True  # everything: also tools without a group
    return g in mode.tools


def allowed_kinds(a: ActionDef, mode: ModeDef) -> set[str]:
    return ({"edit", "delete"} if a.files else set()) | ({"command", "run"} if terminal_on(a, mode) else set())


# ---------- the old "modes" (one list per project) to modes of actions, once ----------
async def migrate() -> None:
    """Before this, every "mode" was what is now an action, and a project had a list of them. A
    custom one (História, Aprendizado…) becomes a MODE with its instructions as the focus; each
    project gets the mode of the custom one it had, or Projeto. Runs once; the old data stays."""
    if await db.mode_scopes.find_one({"key": "migrated_v2"}):
        return
    converted: dict[str, str] = {}
    for d in await db.modes.find({}).to_list(500):
        key = d.get("key", "")
        if key in BUILTIN_ACTIONS:
            if (d.get("instructions") or "").strip() and not await db.actions_defs.find_one({"key": key}):
                await db.actions_defs.insert_one({"key": key, "instructions": d["instructions"]})
            continue
        engine, files, terminal = d.get("engine", "chat"), bool(d.get("files")), bool(d.get("terminal"))
        acts = ["chat"] + (["plan"] if engine in ("plan", "build") or files else []) + (["build"] if files or terminal or engine == "build" else [])
        focus = re.sub(r"^MODO [A-ZÁÉÍÓÚÂÊÔÃÕÇ ]+(\([^)]*\))?:\s*", "", (d.get("instructions") or "").strip())
        tools = [t for t in (d.get("tools") or []) if t in GROUPS and t != "terminal"] + (["terminal"] if terminal else [])
        m = ModeDef(key=_key(d.get("name") or "modo", "modo"), name=(d.get("name") or "Modo")[:40], tagline=(d.get("tagline") or "")[:90],
                    icon=d.get("icon") or "Sparkles", color=d.get("color") or "#818cf8", focus=focus[:6000], actions=acts,
                    tools=tools, template=d.get("template") or "", created_at=d.get("created_at") or now_iso())
        await db.mode_sets.insert_one(m.model_dump())
        converted[key] = m.key
    for p in await db.projects.find({}).to_list(1000):
        if p.get("mode"):
            continue
        old = p.get("modes") or []
        mine = next((converted[k] for k in old if k in converted), None)
        await db.projects.update_one({"_id": p["_id"]}, {"$set": {"mode": mine or DEFAULT_MODE}})
    await db.mode_scopes.insert_one({"key": "migrated_v2", "at": now_iso(), "converted": converted})


# ---------- routes: actions ----------
class ActionIn(BaseModel):
    name: str = Field(min_length=1, max_length=40)
    tagline: str = Field(default="", max_length=90)
    icon: str = Field(default="Sparkles", max_length=40)
    color: str = Field(default="#818cf8", pattern=r"^#[0-9a-fA-F]{6}$")
    engine: str = "chat"
    instructions: str = Field(default="", max_length=6000)
    files: bool = False
    terminal: bool = False
    context: bool = True
    ask: bool = False
    approval: str = "plan"
    template: str = ""


async def _actions_listing() -> dict:
    acts = await all_actions()
    modes = await all_modes()
    return {
        "actions": [{**a.model_dump(), "modes": [m.name for m in modes if a.key in m.actions]} for a in acts],
        "templates": [{"key": k, **{f: t[f] for f in ("name", "tagline", "icon", "color", "engine")},
                       "added": any(a.template == k for a in acts)} for k, t in ACTION_TEMPLATES.items()],
    }


@router.get("/action-defs")
async def list_actions():
    return await _actions_listing()


@router.post("/action-defs")
async def create_action(body: ActionIn):
    a = ActionDef(key=_key(body.name, "acao"), **body.model_dump()).clean()
    await db.actions_defs.insert_one(a.model_dump())
    await audit("mode", f"Ação criada: {a.name}")
    return a.model_dump()


@router.post("/action-defs/templates/{tkey}")
async def add_action_template(tkey: str):
    t = ACTION_TEMPLATES.get(tkey)
    if not t:
        raise HTTPException(404, "Modelo não encontrado")
    a = ActionDef(key=_key(t["name"], "acao"), template=tkey, **t).clean()
    await db.actions_defs.insert_one(a.model_dump())
    return a.model_dump()


async def _get_action(key: str) -> ActionDef:
    a = next((x for x in await all_actions() if x.key == key), None)
    if not a:
        raise HTTPException(404, "Ação não encontrada")
    return a


@router.patch("/action-defs/{key}")
async def update_action(key: str, body: ActionIn):
    cur = await _get_action(key)
    data = body.model_dump()
    if cur.builtin:
        data["engine"] = cur.engine  # a factory action keeps its base
    a = ActionDef(**{**cur.model_dump(), **data, "key": key, "builtin": cur.builtin}).clean()
    if await db.actions_defs.find_one({"key": key}):
        await db.actions_defs.update_one({"key": key}, {"$set": a.model_dump()})
    else:
        await db.actions_defs.insert_one(a.model_dump())
    return a.model_dump()


@router.post("/action-defs/{key}/reset")
async def reset_action(key: str):
    if key not in BUILTIN_ACTIONS:
        raise HTTPException(400, "Só as ações de base voltam ao original")
    await db.actions_defs.delete_many({"key": key})
    return BUILTIN_ACTIONS[key].model_dump()


@router.delete("/action-defs/{key}")
async def delete_action(key: str):
    if key in BUILTIN_ACTIONS:
        raise HTTPException(400, "As ações de base não podem ser apagadas")
    if not await db.actions_defs.find_one({"key": key}):
        raise HTTPException(404, "Ação não encontrada")
    for m in await all_modes():
        if key in m.actions and m.actions == [key]:
            raise HTTPException(409, f"O modo {m.name} só tem esta ação: troque as ações dele antes")
    await db.actions_defs.delete_many({"key": key})
    for m in await all_modes():
        if key in m.actions:
            await _save_mode(m.model_copy(update={"actions": [k for k in m.actions if k != key]}))
    return {"ok": True}


# ---------- routes: modes ----------
class ModeIn(BaseModel):
    name: str = Field(min_length=1, max_length=40)
    tagline: str = Field(default="", max_length=90)
    icon: str = Field(default="Sparkles", max_length=40)
    color: str = Field(default="#818cf8", pattern=r"^#[0-9a-fA-F]{6}$")
    focus: str = Field(default="", max_length=6000)
    actions: list[str] = Field(default=["chat"], max_length=6)
    tools: list[str] = []
    auto_switch: bool = True
    template: str = ""


async def _save_mode(m: ModeDef) -> ModeDef:
    if await db.mode_sets.find_one({"key": m.key}):
        await db.mode_sets.update_one({"key": m.key}, {"$set": m.model_dump()})
    else:
        await db.mode_sets.insert_one(m.model_dump())
    return m


async def _modes_listing() -> dict:
    modes = await all_modes()
    projects = await db.projects.find({}).to_list(1000)
    return {
        "modes": [{**m.model_dump(), "projects": [p.get("name", "") for p in projects if (p.get("mode") or DEFAULT_MODE) == m.key]
                   + (["chat livre"] if m.key == CHAT_MODE else [])} for m in modes],
        "templates": [{"key": k, **{f: t[f] for f in ("name", "tagline", "icon", "color", "actions")},
                       "needs": "Precisa do terminal isolado (só no PC)" if k == "hacking" else "",
                       "added": any(m.template == k for m in modes)} for k, t in MODE_TEMPLATES.items()],
    }


@router.get("/modes")
async def list_modes():
    return await _modes_listing()


@router.get("/modes/current")
async def current_mode(project_id: Optional[str] = None):
    """The mode of a project (or chat livre) with its actions, in order: what the switcher shows."""
    mode = await mode_of(project_id or None)
    acts = {a.key: a for a in await all_actions()}
    return {"mode": mode.model_dump(), "actions": [acts[k].model_dump() for k in mode.actions if k in acts]}


@router.post("/modes")
async def create_mode(body: ModeIn):
    known = {a.key: a for a in await all_actions()}
    m = ModeDef(key=_key(body.name, "modo"), **body.model_dump()).clean(known)
    await db.mode_sets.insert_one(m.model_dump())
    await audit("mode", f"Modo criado: {m.name}")
    return m.model_dump()


@router.post("/modes/templates/{tkey}")
async def add_mode_template(tkey: str):
    t = MODE_TEMPLATES.get(tkey)
    if not t:
        raise HTTPException(404, "Modelo não encontrado")
    known = {a.key: a for a in await all_actions()}
    m = ModeDef(key=_key(t["name"], "modo"), template=tkey, **t).clean(known)
    await db.mode_sets.insert_one(m.model_dump())
    return m.model_dump()


@router.patch("/modes/{key}")
async def update_mode(key: str, body: ModeIn):
    cur = next((m for m in await all_modes() if m.key == key), None)
    if not cur:
        raise HTTPException(404, "Modo não encontrado")
    known = {a.key: a for a in await all_actions()}
    m = ModeDef(**{**cur.model_dump(), **body.model_dump(), "key": key, "builtin": cur.builtin}).clean(known)
    return (await _save_mode(m)).model_dump()


@router.post("/modes/{key}/reset")
async def reset_mode(key: str):
    if key not in BUILTIN_MODES:
        raise HTTPException(400, "Só os modos de fábrica voltam ao original")
    await db.mode_sets.delete_many({"key": key})
    return BUILTIN_MODES[key].model_dump()


@router.delete("/modes/{key}")
async def delete_mode(key: str):
    if key in BUILTIN_MODES:
        raise HTTPException(400, "Os modos de fábrica não podem ser apagados")
    if not await db.mode_sets.find_one({"key": key}):
        raise HTTPException(404, "Modo não encontrado")
    await db.mode_sets.delete_many({"key": key})
    for p in await db.projects.find({"mode": key}).to_list(1000):
        await db.projects.update_one({"_id": p["_id"]}, {"$set": {"mode": DEFAULT_MODE}})
    return {"ok": True}


async def set_project_mode(project_id: str, key: str) -> str:
    if key not in {m.key for m in await all_modes()}:
        raise HTTPException(400, "Modo desconhecido")
    await db.projects.update_one({"_id": oid(project_id)}, {"$set": {"mode": key}})
    return key


class DraftIn(BaseModel):
    description: str = Field(min_length=3, max_length=1500)
    kind: str = "mode"  # mode | action


DRAFT_MODE = """O usuário quer um novo MODO de trabalho para a assistente Lumna (um app de IA pessoal). Um modo é um foco
(instruções que valem para tudo) mais um conjunto de ações: "chat" (conversa e perguntas), "plan" (plano com aprovação)
e "build" (constrói o plano aprovado: arquivos e terminal; exige "plan").
Responda SOMENTE com um objeto JSON, sem texto extra:
{"name": "até 3 palavras", "tagline": "categoria curta", "icon": "um ícone lucide (ex.: BookOpen, GraduationCap, Lightbulb, Code2, Music, Dumbbell, Languages, Sparkles)",
 "color": "#rrggbb", "focus": "o foco do modo, em português, 3 a 8 linhas", "actions": ["chat", "plan", "build"],
 "tools": ["android","pc","web","lights","reminders","terminal"]}
Regras: só "build" se o modo cria arquivos ou código; "terminal" só para programação; tools só as necessárias."""
DRAFT_ACTION = """O usuário quer uma nova AÇÃO para a assistente Lumna, feita a partir de uma ação de base: "chat" (conversa),
"plan" (plano com aprovação) ou "build" (constrói: arquivos e terminal). Responda SOMENTE com um objeto JSON, sem texto extra:
{"name": "até 2 palavras", "tagline": "o que ela faz, curto", "icon": "um ícone lucide", "color": "#rrggbb", "engine": "chat|plan|build",
 "instructions": "como a Lumna age nesta ação, 2 a 6 linhas", "files": true|false, "terminal": true|false, "context": true|false, "ask": true|false}"""


@router.post("/modes/draft")
async def draft(body: DraftIn):
    provider, model = await resolve("planner")
    prompt = DRAFT_ACTION if body.kind == "action" else DRAFT_MODE
    raw = await complete(provider, model, [{"role": "system", "content": prompt}, {"role": "user", "content": body.description}])
    m = re.search(r"\{.*\}", raw, re.S)
    try:
        d = json.loads(m.group(0)) if m else {}
    except json.JSONDecodeError:
        d = {}
    if not isinstance(d, dict) or not d.get("name"):
        raise HTTPException(502, "A Emma não conseguiu montar; tente descrever de outro jeito")
    try:
        if body.kind == "action":
            a = ActionDef(key="draft", **{k: v for k, v in d.items() if k in ActionIn.model_fields}).clean()
            return {k: v for k, v in a.model_dump().items() if k in ActionIn.model_fields}
        known = {a.key: a for a in await all_actions()}
        mo = ModeDef(key="draft", **{k: v for k, v in d.items() if k in ModeIn.model_fields}).clean(known)
        return {k: v for k, v in mo.model_dump().items() if k in ModeIn.model_fields}
    except HTTPException:
        raise
    except Exception:
        raise HTTPException(502, "Veio incompleto; tente de novo")


# ---------- routes: approving a whole plan ----------
async def _plan(mid: str) -> tuple[dict, Project, list[Action]]:
    msg = await db.messages.find_one({"_id": oid(mid)})
    if not msg or not msg.get("action_ids"):
        raise HTTPException(404, "Plano não encontrado")
    acts = [Action.from_mongo(d) for d in await db.actions.find({"_id": {"$in": [oid(a) for a in msg["action_ids"]]}}).to_list(200)]
    order = {a: i for i, a in enumerate(msg["action_ids"])}
    acts.sort(key=lambda a: order.get(a.id, 0))
    p = await get_project(acts[0].project_id) if acts else None
    if not p:
        raise HTTPException(404, "Projeto não encontrado")
    return msg, p, acts


async def _recount_build(msg: dict, acts: list[Action], folder: str = "") -> None:
    """A build's file approved by hand that is one of the plan's (by another spelling) counts as done."""
    b = msg.get("build")
    if not b:
        return
    plan_msg = await db.messages.find_one({"_id": oid(b["plan_id"])})
    if not plan_msg or not plan_msg.get("plan_doc"):
        return
    files = plan_files(plan_msg["plan_doc"])
    hit = {a.path: plan_match(files, a.path, folder) for a in acts if a.status == "executed" and a.type in ("edit", "delete")}
    hit = {p: f for p, f in hit.items() if f}
    if not hit:
        return
    done = list(dict.fromkeys((plan_msg.get("plan_done") or []) + list(hit.values())))
    await db.messages.update_one({"_id": plan_msg["_id"]}, {"$set": {"plan_done": done}})
    b = {**b, "applied": list(dict.fromkeys(b["applied"] + list(hit))), "outside": [p for p in b["outside"] if p not in hit],
         "missing": [p for p in files if p not in done]}
    await db.messages.update_one({"_id": msg["_id"]}, {"$set": {"build": b}})
    msg["build"] = b


@router.post("/plans/{mid}/accept")
async def accept_plan(mid: str):
    """ "Aprovar e construir" on the Planner's card: marks the plan approved; the page then sends the
    build turn with approved_plan=mid (routes_chat), which writes the plan's files without asking."""
    msg = await db.messages.find_one({"_id": oid(mid)})
    if not msg or not msg.get("plan_doc"):
        raise HTTPException(404, "Plano não encontrado")
    if msg.get("plan_doc_status") in ("superseded", "rejected"):
        raise HTTPException(409, "Este plano foi substituído por um mais novo" if msg.get("plan_doc_status") == "superseded" else "Este plano foi rejeitado")
    session = await db.sessions.find_one({"_id": oid(msg["session_id"])})
    mode = await mode_of(session.get("project_id") if session else None)
    target = await build_action(mode)
    if not target:
        raise HTTPException(400, f"O modo {mode.name} não tem uma ação de construir")
    await db.messages.update_one({"_id": oid(mid)}, {"$set": {"plan_doc_status": "approved", "plan_doc_at": now_iso()}})
    await audit("action", f"Plano aprovado: {msg['plan_doc'].get('title', '')}", session.get("project_id") if session else None, message_id=mid)
    return {"id": mid, "plan_doc_status": "approved", "build_action": target}


@router.post("/plans/{mid}/decline")
async def decline_plan(mid: str):
    """ "Rejeitar" on the Planner's card: the plan is set aside and the page goes back to the Chat."""
    msg = await db.messages.find_one({"_id": oid(mid)})
    if not msg or not msg.get("plan_doc"):
        raise HTTPException(404, "Plano não encontrado")
    if msg.get("plan_doc_status") != "proposed":
        raise HTTPException(409, "Este plano não está mais esperando resposta")
    await db.messages.update_one({"_id": oid(mid)}, {"$set": {"plan_doc_status": "rejected", "plan_doc_at": now_iso()}})
    session = await db.sessions.find_one({"_id": oid(msg["session_id"])})
    mode = await mode_of(session.get("project_id") if session else None)
    await audit("action", f"Plano rejeitado: {msg['plan_doc'].get('title', '')}", session.get("project_id") if session else None, message_id=mid)
    return {"id": mid, "plan_doc_status": "rejected", "chat_action": await action_with(mode, "chat")}


@router.post("/plans/{mid}/approve")
async def approve_plan(mid: str):
    """Every pending file change of the answer, in order, with one approval. Commands keep their own cards."""
    from routes_projects import execute_action
    msg, p, acts = await _plan(mid)
    todo = [a for a in acts if a.status == "pending" and a.type in ("edit", "delete")]
    if not todo:
        raise HTTPException(409, "Nada pendente neste plano")
    for a in todo:
        await execute_action(a, p)
    done = sum(a.status == "executed" for a in todo)
    await db.messages.update_one({"_id": oid(mid)}, {"$set": {"plan_status": "executed", "plan_at": now_iso()}})
    await _recount_build(msg, acts, p.folder)
    await audit("action", f"Plano aprovado: {done} de {len(todo)} arquivos", p.id, message_id=mid)
    return {"actions": [a.out() for a in acts], "done": done, "total": len(todo), "build": msg.get("build")}


@router.post("/plans/{mid}/reject")
async def reject_plan(mid: str):
    msg, p, acts = await _plan(mid)
    for a in acts:
        if a.status == "pending" and a.type in ("edit", "delete"):
            await db.actions.update_one({"_id": oid(a.id)}, {"$set": {"status": "rejected", "result": "Plano descartado", "resolved_at": now_iso()}})
            a.status, a.result = "rejected", "Plano descartado"
    await db.messages.update_one({"_id": oid(mid)}, {"$set": {"plan_status": "rejected"}})
    await audit("action", "Plano descartado", p.id, message_id=mid)
    return {"actions": [a.out() for a in acts]}


@router.post("/plans/{mid}/undo")
async def undo_plan(mid: str):
    """Puts back what the plan changed (from the content each action saved before writing). A file
    that changed again since then is left as it is, and said so."""
    msg, p, acts = await _plan(mid)
    undone, kept = 0, []
    for a in reversed(acts):
        if a.status != "executed" or a.type not in ("edit", "delete"):
            continue
        try:
            if a.type == "edit":
                if wr.current_hash(p.folder, a.path) != wr.sha(a.new_content or ""):
                    kept.append(a.path)
                    continue
                if a.base_hash:
                    wr.write_file(p.folder, a.path, a.old_content or "")
                else:
                    wr.delete_file(p.folder, a.path)  # it was created by the plan
            else:
                if wr.safe_path(p.folder, a.path).exists():
                    kept.append(a.path)
                    continue
                wr.write_file(p.folder, a.path, a.old_content or "")
        except HTTPException:
            kept.append(a.path)
            continue
        await db.actions.update_one({"_id": oid(a.id)}, {"$set": {"status": "reverted", "result": "Desfeito com o plano", "resolved_at": now_iso()}})
        a.status, a.result = "reverted", "Desfeito com o plano"
        undone += 1
    await db.messages.update_one({"_id": oid(mid)}, {"$set": {"plan_status": "undone"}})
    await audit("action", f"Plano desfeito: {undone} arquivos" + (f" ({len(kept)} mudaram depois e ficaram)" if kept else ""), p.id, message_id=mid)
    return {"actions": [a.out() for a in acts], "undone": undone, "kept": kept}


# ---------- routes: answering a card of questions ----------
class AnswerIn(BaseModel):
    question: str = Field(max_length=300)
    choices: list[str] = Field(default=[], max_length=4)
    other: str = Field(default="", max_length=1000)


class AnswersIn(BaseModel):
    answers: list[AnswerIn] = Field(min_length=1, max_length=3)


@router.post("/messages/{mid}/answers")
async def save_answers(mid: str, body: AnswersIn):
    """Marks the card answered (it shows what was picked); the answers themselves go as the next message."""
    msg = await db.messages.find_one({"_id": oid(mid)})
    if not msg or not msg.get("questions"):
        raise HTTPException(404, "Perguntas não encontradas")
    answers = [a.model_dump() for a in body.answers]
    await db.messages.update_one({"_id": oid(mid)}, {"$set": {"answers": answers}})
    return {"answers": answers}
