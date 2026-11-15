// O catálogo pessoal da Mídia: o que o usuário salvou para ver depois, os favoritos e o que já assistiu (filmes, séries, vídeos). Cada item guarda o
// título, o serviço, o link, a miniatura e o estado. Fica em <dados>/catalogo.json (escrita atômica) e <dados>/thumbs/cat-<id>.*.
#pragma once
#include <string>
#include <vector>

namespace lumna {

struct Item {
  std::string id;          // hash do endereço (o mesmo link salvo de novo atualiza o mesmo item)
  std::string titulo;
  std::string servico;     // id do serviço (netflix, youtube...) ou "" para um arquivo local
  std::string url;         // o link (https://...) ou o caminho do arquivo local
  std::string origem;      // "link" ou "local"
  std::string tipo;        // "filme", "serie", "video" ou "outro"
  std::string genero;
  std::string miniatura;   // caminho relativo a <dados> ("thumbs/cat-<id>.jpg")
  double duracao_min = 0;  // duração total em minutos (0: não se sabe)
  double pos = 0, dur = 0; // até onde assistiu (segundos), quando o site diz
  bool favorito = false, depois = false, assistido = false;
  long salvo_em = 0;
};

class Catalogo {
 public:
  explicit Catalogo(const std::string& dados);
  std::vector<Item>& itens() { return itens_; }
  Item* Acha(const std::string& id);
  Item* PorUrl(const std::string& url);
  Item* Novo(const std::string& url);   // o item daquele link (cria se não existe)
  bool Remove(const std::string& id);
  bool Grava() const;
  std::string JSON() const;             // {"itens":[...]} com "miniatura_url" (file://) para a interface
  std::string Miniatura(const Item& i) const { return i.miniatura.empty() ? "" : dados_ + "/" + i.miniatura; }
  const std::string& dados() const { return dados_; }
  static std::string IdDaUrl(const std::string& url);

 private:
  std::string dados_;
  std::vector<Item> itens_;
};

// "PT2H18M" / "PT45M" / "PT1H" (ISO 8601) em minutos; 0 se não entender
double DuracaoIsoMin(const std::string& iso);
// a extensão de uma imagem pelos primeiros bytes (".jpg", ".png", ".webp", ".gif"); "" se não parece imagem
std::string ExtensaoDeImagem(const std::string& bytes);

}  // namespace lumna
