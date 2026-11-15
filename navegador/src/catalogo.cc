#include "catalogo.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
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

double Num(CefRefPtr<CefDictionaryValue> d, const char* k) {
  if (!d || !d->HasKey(k)) return 0;
  return d->GetType(k) == VTYPE_INT ? d->GetInt(k) : d->GetType(k) == VTYPE_DOUBLE ? d->GetDouble(k) : 0;
}

}  // namespace

std::string Catalogo::IdDaUrl(const std::string& url) {
  std::string u = url.substr(0, url.find('#'));  // o que vem depois de # não muda a página
  unsigned long long h = 1469598103934665603ull;  // FNV-1a
  for (unsigned char c : u) { h ^= c; h *= 1099511628211ull; }
  char b[24];
  snprintf(b, sizeof b, "%016llx", h);
  return b;
}

Catalogo::Catalogo(const std::string& dados) : dados_(dados) {
  auto v = CefParseJSON(Le(dados_ + "/catalogo.json"), JSON_PARSER_RFC);
  if (!v || v->GetType() != VTYPE_DICTIONARY) return;
  auto l = v->GetDictionary()->GetList("itens");
  if (!l) return;
  for (size_t i = 0; i < l->GetSize(); i++) {
    auto d = l->GetDictionary(i);
    if (!d) continue;
    Item x;
    x.id = d->GetString("id"); x.titulo = d->GetString("titulo"); x.servico = d->GetString("servico"); x.url = d->GetString("url");
    x.origem = d->GetString("origem"); x.tipo = d->GetString("tipo"); x.genero = d->GetString("genero"); x.miniatura = d->GetString("miniatura");
    x.duracao_min = Num(d, "duracao_min"); x.pos = Num(d, "pos"); x.dur = Num(d, "dur");
    x.favorito = d->GetBool("favorito"); x.depois = d->GetBool("depois"); x.assistido = d->GetBool("assistido");
    x.salvo_em = static_cast<long>(Num(d, "salvo_em"));
    if (!x.id.empty() && !x.url.empty()) itens_.push_back(x);
  }
}

Item* Catalogo::Acha(const std::string& id) {
  for (auto& i : itens_) if (i.id == id) return &i;
  return nullptr;
}

Item* Catalogo::PorUrl(const std::string& url) { return Acha(IdDaUrl(url)); }

Item* Catalogo::Novo(const std::string& url) {
  if (Item* e = PorUrl(url)) return e;
  Item x;
  x.id = IdDaUrl(url);
  x.url = url.substr(0, url.find('#'));
  x.origem = "link";
  x.tipo = "outro";
  x.salvo_em = static_cast<long>(time(nullptr));
  itens_.push_back(x);
  return &itens_.back();
}

bool Catalogo::Remove(const std::string& id) {
  for (size_t k = 0; k < itens_.size(); k++) {
    if (itens_[k].id == id) {
      if (!itens_[k].miniatura.empty()) remove(Miniatura(itens_[k]).c_str());
      itens_.erase(itens_.begin() + k);
      return true;
    }
  }
  return false;
}

std::string Catalogo::JSON() const {
  std::string o = "{\"itens\":[";
  for (size_t k = 0; k < itens_.size(); k++) {
    const Item& i = itens_[k];
    char n[200];
    snprintf(n, sizeof n, "\"duracao_min\":%.1f,\"pos\":%.1f,\"dur\":%.1f,\"salvo_em\":%ld", i.duracao_min, i.pos, i.dur, i.salvo_em);
    o += std::string(k ? "," : "") + "{\"id\":" + J(i.id) + ",\"titulo\":" + J(i.titulo) + ",\"servico\":" + J(i.servico) + ",\"url\":" + J(i.url) +
         ",\"origem\":" + J(i.origem) + ",\"tipo\":" + J(i.tipo) + ",\"genero\":" + J(i.genero) + ",\"miniatura\":" + J(i.miniatura) +
         ",\"miniatura_url\":" + J(i.miniatura.empty() ? "" : "file://" + Miniatura(i)) + "," + n +
         ",\"favorito\":" + (i.favorito ? "true" : "false") + ",\"depois\":" + (i.depois ? "true" : "false") + ",\"assistido\":" + (i.assistido ? "true" : "false") + "}";
  }
  return o + "]}";
}

bool Catalogo::Grava() const {
  std::string j = JSON(), tmp = dados_ + "/catalogo.json.tmp";
  std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f << j;
  f.close();
  return rename(tmp.c_str(), (dados_ + "/catalogo.json").c_str()) == 0;
}

double DuracaoIsoMin(const std::string& iso) {
  if (iso.size() < 3 || iso[0] != 'P') return 0;
  size_t t = iso.find('T');
  if (t == std::string::npos) return 0;
  double min = 0, n = 0;
  std::string num;
  for (size_t k = t + 1; k < iso.size(); k++) {
    char c = iso[k];
    if ((c >= '0' && c <= '9') || c == '.') num += c;
    else {
      n = atof(num.c_str());
      num.clear();
      if (c == 'H') min += n * 60;
      else if (c == 'M') min += n;
      else if (c == 'S') min += n / 60;
    }
  }
  return min;
}

std::string ExtensaoDeImagem(const std::string& b) {
  if (b.size() >= 3 && (unsigned char)b[0] == 0xFF && (unsigned char)b[1] == 0xD8) return ".jpg";
  if (b.size() >= 8 && b.compare(0, 4, "\x89PNG") == 0) return ".png";
  if (b.size() >= 12 && b.compare(0, 4, "RIFF") == 0 && b.compare(8, 4, "WEBP") == 0) return ".webp";
  if (b.size() >= 6 && b.compare(0, 3, "GIF") == 0) return ".gif";
  return "";
}

}  // namespace lumna
