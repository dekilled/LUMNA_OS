#include "servicos.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "include/cef_parser.h"
#include "include/cef_values.h"

namespace lumna {
namespace {

std::string Le(const std::string& caminho) {
  std::ifstream f(caminho, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

std::string J(const std::string& s) {
  std::string o = "\"";
  for (unsigned char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; } else o += static_cast<char>(c);
    }
  }
  return o + "\"";
}

Servico DeDicionario(CefRefPtr<CefDictionaryValue> d) {
  Servico sv;
  sv.id = d->GetString("id"); sv.nome = d->GetString("nome"); sv.url = d->GetString("url");
  if (auto dl = d->GetList("dominios"))
    for (size_t k = 0; k < dl->GetSize(); k++) sv.dominios.push_back(dl->GetString(k));
  if (d->HasKey("cor") && !d->GetString("cor").empty()) sv.cor = d->GetString("cor");
  if (d->HasKey("modo") && !d->GetString("modo").empty()) sv.modo = d->GetString("modo");
  if (d->HasKey("voltar") && !d->GetString("voltar").empty()) sv.voltar = d->GetString("voltar");
  sv.favorito = d->HasKey("favorito") && d->GetBool("favorito");
  sv.ua_tv = d->HasKey("ua_tv") && d->GetBool("ua_tv");
  return sv;
}

std::vector<Servico> DeJSON(const std::string& texto) {
  std::vector<Servico> v;
  auto val = CefParseJSON(texto, JSON_PARSER_RFC);
  if (!val) return v;
  CefRefPtr<CefListValue> l;
  if (val->GetType() == VTYPE_LIST) l = val->GetList();
  else if (val->GetType() == VTYPE_DICTIONARY) l = val->GetDictionary()->GetList("servicos");
  for (size_t i = 0; l && i < l->GetSize(); i++) {
    auto d = l->GetDictionary(i);
    if (d && !d->GetString("id").empty() && !d->GetString("url").empty()) v.push_back(DeDicionario(d));
  }
  return v;
}

}  // namespace

std::string HostDe(const std::string& url) {
  CefURLParts partes;
  if (!CefParseURL(url, partes)) return "";
  return CefString(&partes.host).ToString();
}

bool UrlDoServico(const Servico& sv, const std::string& url) {
  if (sv.dominios.empty()) return true;
  std::string host = HostDe(url);
  if (host.empty()) return false;
  for (auto& d : sv.dominios)
    if (host == d || (host.size() > d.size() && host.compare(host.size() - d.size() - 1, d.size() + 1, "." + d) == 0)) return true;
  return false;
}

std::string DominioBase(const std::string& host) {
  std::vector<std::string> p;
  std::stringstream ss(host);
  for (std::string x; std::getline(ss, x, '.');) p.push_back(x);
  if (p.size() <= 2) return host;
  static const char* kSegundo[] = {"com", "co", "org", "net", "gov", "edu"};  // com.br, co.uk, org.au...
  bool segundo = p[p.size() - 2].size() <= 3 && std::find_if(std::begin(kSegundo), std::end(kSegundo), [&](const char* c) { return p[p.size() - 2] == c; }) != std::end(kSegundo) && p.back().size() == 2;
  size_t n = segundo ? 3 : 2;
  std::string r;
  for (size_t i = p.size() - n; i < p.size(); i++) r += (r.empty() ? "" : ".") + p[i];
  return r;
}

std::string CorDoNome(const std::string& nome) {
  unsigned h = 2166136261u;
  for (unsigned char c : nome) { h ^= c; h *= 16777619u; }
  static const char* paleta[] = {"#ff6b6b", "#ffa94d", "#ffd43b", "#69db7c", "#38d9a9", "#3bc9db", "#4dabf7", "#748ffc", "#b197fc", "#f783ac"};
  return paleta[h % 10];
}

std::string IdUnico(const std::vector<Servico>& v, const std::string& nome) {
  std::string base;
  for (unsigned char c : nome) {
    if (isalnum(c)) base += static_cast<char>(tolower(c));
    else if (!base.empty() && base.back() != '-') base += '-';
  }
  while (!base.empty() && base.back() == '-') base.pop_back();
  if (base.empty()) base = "site";
  auto existe = [&](const std::string& id) { return std::any_of(v.begin(), v.end(), [&](const Servico& s) { return s.id == id; }); };
  std::string id = base;
  for (int n = 2; existe(id); n++) id = base + "-" + std::to_string(n);
  return id;
}

const std::vector<Servico>& Biblioteca() {
  static const std::vector<Servico> b = [] {
    std::vector<Servico> v;
    auto add = [&](const char* id, const char* nome, const char* url, std::vector<std::string> dom, const char* cor, bool tv = false, const char* voltar = "historico") {
      Servico s;
      s.id = id; s.nome = nome; s.url = url; s.dominios = std::move(dom); s.cor = cor; s.ua_tv = tv; s.voltar = voltar;
      v.push_back(s);
    };
    // o YouTube só serve a interface de TV (youtube.com/tv) a quem se anuncia como TV; ali o "voltar" é o Escape
    add("youtube", "YouTube", "https://www.youtube.com/tv", {"youtube.com", "youtu.be"}, "#ff3b3b", true, "esc");
    add("netflix", "Netflix", "https://www.netflix.com/browse", {"netflix.com"}, "#ff2a35");
    add("disney", "Disney+", "https://www.disneyplus.com/", {"disneyplus.com"}, "#5b9bff");
    add("prime", "Prime Video", "https://www.primevideo.com/", {"primevideo.com", "amazon.com"}, "#2fc0ee");
    add("hbomax", "HBO Max", "https://www.hbomax.com/", {"hbomax.com", "max.com"}, "#a78bfa");
    add("paramount", "Paramount+", "https://www.paramountplus.com/", {"paramountplus.com"}, "#5a8cff");
    add("appletv", "Apple TV+", "https://tv.apple.com/", {"apple.com"}, "#e9e9f2");
    add("globoplay", "Globoplay", "https://globoplay.globo.com/", {"globoplay.globo.com", "globo.com"}, "#ff6a4d");
    add("crunchyroll", "Crunchyroll", "https://www.crunchyroll.com/", {"crunchyroll.com"}, "#ff8a3d");
    add("twitch", "Twitch", "https://www.twitch.tv/", {"twitch.tv"}, "#b08cff");
    add("plex", "Plex", "https://app.plex.tv/", {"plex.tv"}, "#f3b73b");
    add("pluto", "Pluto TV", "https://pluto.tv/", {"pluto.tv"}, "#ffe066");
    add("tubi", "Tubi", "https://tubitv.com/", {"tubitv.com"}, "#ff7f50");
    add("mubi", "MUBI", "https://mubi.com/", {"mubi.com"}, "#dcdcdc");
    add("telecine", "Telecine", "https://www.telecineplay.com.br/", {"telecineplay.com.br"}, "#ff9a3d");
    add("looke", "Looke", "https://www.looke.com.br/", {"looke.com.br"}, "#ff5c8a");
    add("vimeo", "Vimeo", "https://vimeo.com/", {"vimeo.com"}, "#4cc9f0");
    return v;
  }();
  return b;
}

std::vector<Servico> PadraoServicos() {
  const char* arq = getenv("LUMNA_NAV_SERVICOS");  // um JSON com outra lista (testes, ou o usuário)
  if (arq && *arq) {
    auto v = DeJSON(Le(arq));
    if (!v.empty()) return v;
  }
  std::vector<Servico> v;
  for (const char* id : {"youtube", "netflix", "disney", "paramount", "prime", "hbomax"})
    for (auto& b : Biblioteca()) if (b.id == id) v.push_back(b);
  return v;
}

std::vector<Servico> CarregaServicos(const std::string& dados) {
  auto v = DeJSON(Le(dados + "/servicos.json"));  // a lista do usuário (depois da primeira mudança)
  return v.empty() ? PadraoServicos() : v;
}

std::string ServicoJSON(const Servico& s) {
  std::string d = "[";
  for (size_t i = 0; i < s.dominios.size(); i++) d += (i ? "," : "") + J(s.dominios[i]);
  return "{\"id\":" + J(s.id) + ",\"nome\":" + J(s.nome) + ",\"url\":" + J(s.url) + ",\"dominios\":" + d + "],\"cor\":" + J(s.cor) + ",\"modo\":" + J(s.modo) +
         ",\"voltar\":" + J(s.voltar) + ",\"favorito\":" + (s.favorito ? "true" : "false") + ",\"ua_tv\":" + (s.ua_tv ? "true" : "false") + "}";
}

bool GravaServicos(const std::string& dados, const std::vector<Servico>& v) {
  std::string j = "{\"servicos\":[";
  for (size_t i = 0; i < v.size(); i++) j += (i ? "," : "") + ServicoJSON(v[i]);
  j += "]}";
  std::string tmp = dados + "/servicos.json.tmp";
  std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f << j;
  f.close();
  return rename(tmp.c_str(), (dados + "/servicos.json").c_str()) == 0;
}

}  // namespace lumna
