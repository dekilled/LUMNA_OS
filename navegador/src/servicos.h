// Os streamings da Mídia: a lista do usuário (<dados>/servicos.json), a biblioteca de streamings conhecidos para adicionar, o favorito e o modo de controle.
#pragma once
#include <string>
#include <vector>

namespace lumna {

// Um streaming. `modo`: como o controle age no site: "setas" (o direcional e o analógico movem o foco entre os itens clicáveis e A clica: o jeito de TV, o padrão)
// ou "cursor" (o analógico move um ponteiro). `voltar` (o botão B): "historico" volta a página; "esc" manda Escape (a interface de TV do YouTube volta assim).
// `ua_tv`: o site só serve a interface de TV a quem se anuncia como TV; o User-Agent de TV vai só nos pedidos aos domínios dele (o login do Google fica de fora).
struct Servico {
  std::string id, nome, url;
  std::vector<std::string> dominios;  // vazio = aceita qualquer endereço (testes)
  std::string cor = "#cfc4ff", modo = "setas", voltar = "historico";
  bool favorito = false, ua_tv = false;
};

// O endereço pertence mesmo a este serviço? (o host é o domínio ou um subdomínio dele)
bool UrlDoServico(const Servico& sv, const std::string& url);
// O host de um endereço ("www.netflix.com")
std::string HostDe(const std::string& url);
// O domínio "de cima" de um host para guardar como domínio do serviço ("www.netflix.com" -> "netflix.com", "www.looke.com.br" -> "looke.com.br")
std::string DominioBase(const std::string& host);

std::vector<Servico> PadraoServicos();           // os que vêm de fábrica (ou os de LUMNA_NAV_SERVICOS, um JSON, nos testes)
const std::vector<Servico>& Biblioteca();        // os streamings conhecidos, para o botão "Adicionar"
std::vector<Servico> CarregaServicos(const std::string& dados);  // <dados>/servicos.json se existir; senão os padrão
bool GravaServicos(const std::string& dados, const std::vector<Servico>& v);
std::string ServicoJSON(const Servico& s);       // um objeto JSON (para a interface e para o arquivo)
std::string IdUnico(const std::vector<Servico>& v, const std::string& nome);   // "Minha TV" -> "minha-tv" (e "minha-tv-2" se já existe)
std::string CorDoNome(const std::string& nome);  // uma cor agradável e estável para um streaming sem cor

}  // namespace lumna
