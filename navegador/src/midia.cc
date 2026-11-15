// Versão "Mídia" do navegador do LUMNA.
//
// Uma janela SEM moldura com duas camadas (CEF Views):
//   - "shell": a interface do LUMNA em HTML (ui/midia/), por baixo. É o início (atalhos, "Continuar assistindo") e o menu de serviços.
//   - "conteúdo": o site do serviço (YouTube, Netflix...), por cima. Só UM serviço fica vivo por vez.
// Estados:   INÍCIO (só o shell)  ->  SERVIÇO (o site cobre tudo)  <->  MENU (o site encolhe, o menu de serviços sobe, o site segue tocando).
// Trocar de serviço: guarda o último endereço, o progresso e um print do site, pausa, e o site expande já no serviço novo.
#include "midia.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_parser.h"
#include "include/cef_task.h"
#include "include/cef_values.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"
#include "include/wrapper/cef_message_router.h"

namespace lumna {
namespace {

// ---------------------------------------------------------------------------------------------------------------------------- utilidades
std::string Env(const char* nome, const char* padrao = "") {
  const char* v = getenv(nome);
  return (v && *v) ? v : padrao;
}
bool Depurando() { return getenv("LUMNA_NAV_DEBUG") != nullptr; }
void Log(const std::string& m) {
  if (Depurando()) {
    printf("NAV %s\n", m.c_str());
    fflush(stdout);
  }
}
void Mkdirs(const std::string& caminho) {
  for (size_t i = 1; i <= caminho.size(); i++)
    if (i == caminho.size() || caminho[i] == '/') mkdir(caminho.substr(0, i).c_str(), 0755);
}
std::string LerArquivo(const std::string& caminho) {
  std::ifstream f(caminho, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}
bool EscreverArquivo(const std::string& caminho, const void* dados, size_t n) {
  std::string tmp = caminho + ".tmp";  // escreve inteiro e renomeia: nunca deixa um arquivo pela metade
  std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(static_cast<const char*>(dados), static_cast<std::streamsize>(n));
  f.close();
  return rename(tmp.c_str(), caminho.c_str()) == 0;
}
std::string J(const std::string& s) {  // string JSON escapada
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
double Num(CefRefPtr<CefDictionaryValue> d, const char* chave) {
  if (!d || !d->HasKey(chave)) return 0;
  switch (d->GetType(chave)) {
    case VTYPE_INT: return d->GetInt(chave);
    case VTYPE_DOUBLE: return d->GetDouble(chave);
    default: return 0;
  }
}
std::string ExeDir() {
  char b[4096];
  ssize_t n = readlink("/proc/self/exe", b, sizeof b - 1);
  if (n <= 0) return ".";
  b[n] = 0;
  std::string s = b;
  return s.substr(0, s.rfind('/'));
}
double Facilita(double t) { return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2; }  // easeInOutCubic

// ---------------------------------------------------------------------------------------------------------------------------- dados
struct Servico { std::string id, nome, url; std::vector<std::string> dominios; };  // dominios vazio = aceita qualquer endereço (testes)
struct Salvo { std::string url, titulo, thumb; double pos = 0, dur = 0; long quando = 0; };

// O endereço pertence mesmo a este serviço? (o host é o domínio ou um subdomínio dele). Evita gravar, com o nome de um serviço, a página de outro.
bool UrlDoServico(const Servico& sv, const std::string& url) {
  if (sv.dominios.empty()) return true;
  CefURLParts partes;
  if (!CefParseURL(url, partes)) return false;
  std::string host = CefString(&partes.host).ToString();
  for (auto& d : sv.dominios)
    if (host == d || (host.size() > d.size() && host.compare(host.size() - d.size() - 1, d.size() + 1, "." + d) == 0)) return true;
  return false;
}

std::vector<Servico> CarregaServicos() {
  std::vector<Servico> v = {
      {"youtube", "YouTube", "https://www.youtube.com/", {"youtube.com", "youtu.be"}},
      {"netflix", "Netflix", "https://www.netflix.com/browse", {"netflix.com"}},
      {"disney", "Disney+", "https://www.disneyplus.com/", {"disneyplus.com"}},
      {"paramount", "Paramount+", "https://www.paramountplus.com/", {"paramountplus.com"}},
      {"prime", "Prime Video", "https://www.primevideo.com/", {"primevideo.com", "amazon.com"}},
      {"hbomax", "HBO Max", "https://www.hbomax.com/", {"hbomax.com", "max.com"}},
  };
  std::string arq = Env("LUMNA_NAV_SERVICOS");  // JSON [{"id","nome","url"}]: outra lista (testes, ou o usuário)
  if (!arq.empty()) {
    auto val = CefParseJSON(LerArquivo(arq), JSON_PARSER_RFC);
    if (val && val->GetType() == VTYPE_LIST) {
      v.clear();
      auto l = val->GetList();
      for (size_t i = 0; i < l->GetSize(); i++) {
        auto d = l->GetDictionary(i);
        if (!d) continue;
        Servico sv{d->GetString("id"), d->GetString("nome"), d->GetString("url"), {}};
        if (auto dl = d->GetList("dominios"))
          for (size_t k = 0; k < dl->GetSize(); k++) sv.dominios.push_back(dl->GetString(k));
        v.push_back(sv);
      }
    }
  }
  return v;
}

// ---------------------------------------------------------------------------------------------------------------------------- a Mídia
class Midia;
CefRefPtr<Midia> g_midia;

enum class Estado { kInicio, kServico, kMenu };
const char* NomeEstado(Estado e) { return e == Estado::kInicio ? "inicio" : e == Estado::kServico ? "servico" : "menu"; }
constexpr int kCmdMenu = 1, kCmdInicio = 2;

class Cliente;

class Midia : public CefWindowDelegate, public CefMessageRouterBrowserSide::Handler, public CefDevToolsMessageObserver {
 public:
  Midia() {
    dados_ = Env("LUMNA_NAV_DADOS");
    if (dados_.empty()) {
      std::string xdg = Env("XDG_DATA_HOME");
      dados_ = xdg.empty() ? Env("HOME", "/tmp") + "/.local/share/lumna/navegador" : xdg + "/lumna/navegador";
    }
    Mkdirs(dados_ + "/thumbs");
    ui_ = Env("LUMNA_NAV_UI", (ExeDir() + "/ui/midia").c_str());
    servicos_ = CarregaServicos();
    CarregaSalvos();
    // Como o site muda de tamanho: "imagem" (padrão) anima um print na interface e só redimensiona o site UMA vez, no fim (o site redimensionado a
    // cada quadro, ao vivo, dá flick em páginas pesadas); "instante" pula direto ao tamanho final; "real" redimensiona o site a cada quadro.
    if (getenv("LUMNA_NAV_SEM_ANIMACAO")) modo_ = ModoAnim::kInstante;
    else if (Env("LUMNA_NAV_ANIMACAO") == "real") modo_ = ModoAnim::kReal;
  }

  void Iniciar();  // cria a janela

  // ----- chamados pelos clientes das visões
  bool Ehshell(CefRefPtr<CefBrowser> b) const { return shell_ && shell_->GetBrowser() && shell_->GetBrowser()->IsSame(b); }
  void Navegar(const std::string& url) {  // pop-ups e links "nova janela" dos sites abrem na mesma visão
    if (conteudo_ && conteudo_->GetBrowser()) conteudo_->GetBrowser()->GetMainFrame()->LoadURL(url);
  }
  void TituloMudou(const std::string& t) {
    if (window_ && estado_ != Estado::kInicio && !t.empty()) window_->SetTitle(t);
  }
  CefRefPtr<CefMessageRouterBrowserSide> roteador() { return roteador_; }
  void SiteCarregou(const std::string& url) {  // a página do serviço terminou de carregar (ou deu erro): se a transição esperava por ela, termina
    if (!esperando_carga_ || carregou_) return;
    if (!ignora_url_.empty() && url == ignora_url_) return;  // o fim de carga da página ANTERIOR não conta
    carregou_ = true;
    TentaFinalizar();
  }

  // ----- janela (CefWindowDelegate)
  void OnWindowCreated(CefRefPtr<CefWindow> w) override;
  void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
    shell_ = nullptr;
    conteudo_ = nullptr;
    window_ = nullptr;
    CefQuitMessageLoop();
  }
  bool CanClose(CefRefPtr<CefWindow>) override { return true; }
  cef_runtime_style_t GetWindowRuntimeStyle() override { return CEF_RUNTIME_STYLE_CHROME; }  // hospeda 1 BrowserView Chrome (o site) + os Alloy
  bool IsFrameless(CefRefPtr<CefWindow>) override { return true; }  // a Mídia desenha tudo; sem moldura nem barra do Orbit
  CefSize GetPreferredSize(CefRefPtr<CefView>) override {
    int l = std::atoi(Env("LUMNA_NAV_LARGURA", "1280").c_str()), a = std::atoi(Env("LUMNA_NAV_ALTURA", "720").c_str());
    return CefSize(l, a);
  }
  void OnWindowBoundsChanged(CefRefPtr<CefWindow>, const CefRect&) override { Aplicar(); }
  void OnLayoutChanged(CefRefPtr<CefView>, const CefRect&) override { Aplicar(); }
  bool OnAccelerator(CefRefPtr<CefWindow>, int cmd) override {
    if (cmd == kCmdMenu) { estado_ == Estado::kMenu ? FecharMenu() : AbrirMenu(); return true; }
    if (cmd == kCmdInicio) { Inicio(); return true; }
    return false;
  }

  // ----- ponte JS (só a interface do LUMNA, file://, é aceita)
  bool OnQuery(CefRefPtr<CefBrowser> b, CefRefPtr<CefFrame> f, int64_t, const CefString& pedido, bool,
               CefRefPtr<Callback> cb) override;

  // ----- DevTools: respostas dos métodos que mandamos ao site (print e progresso)
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser>, int id, bool ok, const void* res, size_t n) override {
    auto it = chamadas_.find(id);
    if (it == chamadas_.end()) return;
    auto cb = it->second;
    chamadas_.erase(it);
    CefRefPtr<CefDictionaryValue> d;
    if (ok && res && n) {
      auto v = CefParseJSON(res, n, JSON_PARSER_RFC);
      if (v && v->GetType() == VTYPE_DICTIONARY) d = v->GetDictionary();
    }
    cb(ok, d);
  }

 private:
  friend class AnimTask;
  using Retorno = std::function<void(bool, CefRefPtr<CefDictionaryValue>)>;

  // --- persistência
  void CarregaSalvos() {
    auto v = CefParseJSON(LerArquivo(dados_ + "/midia.json"), JSON_PARSER_RFC);
    if (!v || v->GetType() != VTYPE_DICTIONARY) return;
    auto s = v->GetDictionary()->GetDictionary("salvos");
    if (!s) return;
    CefDictionaryValue::KeyList chaves;
    s->GetKeys(chaves);
    for (auto& k : chaves) {
      auto e = s->GetDictionary(k);
      if (!e) continue;
      Salvo x;
      x.url = e->GetString("url"); x.titulo = e->GetString("titulo"); x.thumb = e->GetString("thumb");
      x.pos = Num(e, "pos"); x.dur = Num(e, "dur"); x.quando = static_cast<long>(Num(e, "quando"));
      const Servico* sv = Acha(k.ToString());
      if (!sv) continue;  // serviço que não existe mais
      if (!UrlDoServico(*sv, x.url)) {  // gravado errado por uma versão antiga: joga fora em vez de abrir a página de outro serviço
        Log("descartado " + k.ToString() + ": o endereço " + x.url + " não é deste serviço");
        continue;
      }
      salvos_[k.ToString()] = x;
    }
  }
  std::string SalvoJSON(const std::string& id, const Salvo& x) const {
    char b[160];
    snprintf(b, sizeof b, ",\"pos\":%.1f,\"dur\":%.1f,\"quando\":%ld", x.pos, x.dur, x.quando);
    return "\"" + id + "\":{\"url\":" + J(x.url) + ",\"titulo\":" + J(x.titulo) + ",\"thumb\":" + J(x.thumb) + b +
           ",\"thumb_url\":" + J(x.thumb.empty() ? "" : "file://" + dados_ + "/" + x.thumb) + "}";
  }
  std::string SalvosJSON() const {
    std::string o = "{";
    bool primeiro = true;
    for (auto& [id, x] : salvos_) { o += (primeiro ? "" : ",") + SalvoJSON(id, x); primeiro = false; }
    return o + "}";
  }
  void Gravar() {
    std::string j = "{\"salvos\":" + SalvosJSON() + "}";
    EscreverArquivo(dados_ + "/midia.json", j.data(), j.size());
  }
  std::string EstadoJSON() const {
    std::string o = "{\"servicos\":[";
    for (size_t i = 0; i < servicos_.size(); i++)
      o += (i ? "," : "") + std::string("{\"id\":") + J(servicos_[i].id) + ",\"nome\":" + J(servicos_[i].nome) + ",\"url\":" + J(servicos_[i].url) + "}";
    return o + "],\"salvos\":" + SalvosJSON() + ",\"ativo\":" + J(ativo_) + ",\"estado\":" + J(NomeEstado(estado_)) + "}";
  }
  const Servico* Acha(const std::string& id) const {
    for (auto& s : servicos_) if (s.id == id) return &s;
    return nullptr;
  }

  // --- layout e animação
  CefRect Cheio() const {
    CefSize t = window_ ? window_->GetSize() : CefSize(1280, 720);
    return CefRect(0, 0, t.width, t.height);
  }
  CefRect Encolhido() const {  // a proporção do esboço: o vídeo no alto, o menu de serviços embaixo
    CefRect c = Cheio();
    return CefRect(static_cast<int>(c.width * 0.14), static_cast<int>(c.height * 0.044), static_cast<int>(c.width * 0.72), static_cast<int>(c.height * 0.613));
  }
  CefRect Alvo() const { return estado_ == Estado::kMenu ? Encolhido() : Cheio(); }
  void Aplicar() {
    if (!window_ || !shell_) return;
    shell_->SetBounds(Cheio());
    if (conteudo_ && !animando_ && !ocupado_) conteudo_->SetBounds(Alvo());
  }
  void Animar(CefRect de, CefRect para, int ms = 320) {
    anim_de_ = de; anim_para_ = para; anim_ms_ = getenv("LUMNA_NAV_SEM_ANIMACAO") ? 1 : ms;
    anim_t0_ = std::chrono::steady_clock::now();
    if (!animando_) { animando_ = true; Passo(); }
    anim_geracao_++;
  }
  void Passo();

  // --- transição do site (ver Transicao)
  enum class ModoAnim { kImagem, kInstante, kReal };
  void Transicao(CefRect de, CefRect para, const std::string& imagem_b64, const std::string& nome, bool esperar_carga);
  void TentaFinalizar();
  void CancelaTransicao();
  void Finalizar();
  std::string DataUrl(const void* dados, size_t n) const { return "data:image/jpeg;base64," + CefBase64Encode(dados, n).ToString(); }
  std::string FracaoJSON(const CefRect& r) const {
    CefRect c = Cheio();
    char b[200];
    snprintf(b, sizeof b, "{\"x\":%.5f,\"y\":%.5f,\"w\":%.5f,\"h\":%.5f}", double(r.x) / c.width, double(r.y) / c.height, double(r.width) / c.width, double(r.height) / c.height);
    return b;
  }

  // --- ações
  void ShellJS(const std::string& js) {
    if (shell_ && shell_->GetBrowser()) shell_->GetBrowser()->GetMainFrame()->ExecuteJavaScript(js, "lumna://shell", 0);
  }
  void Mudar(Estado e) {
    estado_ = e;
    Log(std::string("estado ") + NomeEstado(e) + (ativo_.empty() ? "" : " " + ativo_));
  }
  void Chamar(const char* metodo, CefRefPtr<CefDictionaryValue> params, Retorno cb) {
    if (!conteudo_ || !conteudo_->GetBrowser()) { cb(false, nullptr); return; }
    auto host = conteudo_->GetBrowser()->GetHost();
    if (!registro_) registro_ = host->AddDevToolsMessageObserver(this);
    int id = ++seq_;
    chamadas_[id] = cb;
    host->ExecuteDevToolsMethod(id, metodo, params);
  }
  void Capturar(std::function<void()> depois = nullptr);
  void IrParaInicio();
  void Guardar();
  void AbrirServico(const std::string& id, const std::string& url);
  void AbrirMenu();
  void FecharMenu();
  void Trocar(const std::string& id);
  void Inicio();

  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefBrowserView> shell_, conteudo_;
  CefRefPtr<CefClient> cliente_shell_, cliente_conteudo_;
  CefRefPtr<CefMessageRouterBrowserSide> roteador_;
  CefRefPtr<CefRegistration> registro_;
  std::string dados_, ui_, ativo_;
  std::vector<Servico> servicos_;
  std::map<std::string, Salvo> salvos_;
  Estado estado_ = Estado::kInicio;
  // animação
  bool animando_ = false;
  ModoAnim modo_ = ModoAnim::kImagem;
  bool ocupado_ = false, esperando_carga_ = false, carregou_ = true, fim_js_ = false;
  int trans_gen_ = 0;
  CefRect destino_;
  std::string ignora_url_;
  int anim_geracao_ = 0, anim_ms_ = 320;
  CefRect anim_de_, anim_para_;
  std::chrono::steady_clock::time_point anim_t0_;
  // captura feita quando o menu abre (o site ainda está em tamanho cheio, tocando)
  std::vector<unsigned char> thumb_;
  std::string info_titulo_, info_url_;  // info_url_: o endereço da página de que a captura é (location.href)
  double info_pos_ = 0, info_dur_ = 0;
  bool captura_ok_ = false;
  int pendentes_ = 0, geracao_cap_ = 0;
  std::function<void()> depois_;
  // DevTools
  int seq_ = 0;
  std::map<int, Retorno> chamadas_;

  IMPLEMENT_REFCOUNTING(Midia);
  DISALLOW_COPY_AND_ASSIGN(Midia);
};

// A janela é estilo Chrome e só admite UM BrowserView Chrome: o do site (precisa do Widevine automático e dos componentes do Chrome).
// A nossa interface em HTML é um BrowserView estilo Alloy (leve, sem a UI do Chrome), que a mesma janela aceita ao lado.
class DelegadoShell : public CefBrowserViewDelegate {
 public:
  cef_runtime_style_t GetBrowserRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }
  IMPLEMENT_REFCOUNTING(DelegadoShell);
};

class FuncTask : public CefTask {
 public:
  explicit FuncTask(std::function<void()> f) : f_(std::move(f)) {}
  void Execute() override { f_(); }
 private:
  std::function<void()> f_;
  IMPLEMENT_REFCOUNTING(FuncTask);
};

// um passo da animação do site (a cada ~16 ms, na thread de interface)
class AnimTask : public CefTask {
 public:
  explicit AnimTask(CefRefPtr<Midia> m) : m_(m) {}
  void Execute() override { m_->Passo(); }
 private:
  CefRefPtr<Midia> m_;
  IMPLEMENT_REFCOUNTING(AnimTask);
};

void Midia::Passo() {
  if (!conteudo_) { animando_ = false; return; }
  double t = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - anim_t0_).count() / anim_ms_;
  if (t >= 1) {
    animando_ = false;
    conteudo_->SetBounds(anim_para_);
    Log("site fim da animação " + std::to_string(anim_para_.x) + "," + std::to_string(anim_para_.y) + " " + std::to_string(anim_para_.width) + "x" + std::to_string(anim_para_.height));
    if (estado_ == Estado::kInicio) conteudo_->SetVisible(false);
    return;
  }
  double e = Facilita(std::max(0.0, t));
  auto lerp = [e](int a, int b) { return static_cast<int>(std::lround(a + (b - a) * e)); };
  conteudo_->SetBounds(CefRect(lerp(anim_de_.x, anim_para_.x), lerp(anim_de_.y, anim_para_.y), lerp(anim_de_.width, anim_para_.width),
                               lerp(anim_de_.height, anim_para_.height)));
  CefPostDelayedTask(TID_UI, new AnimTask(this), 16);
}

// ---------------------------------------------------------------------------------------------------------------------------- clientes das visões
class Cliente : public CefClient, public CefLifeSpanHandler, public CefRequestHandler, public CefDisplayHandler, public CefLoadHandler {
 public:
  Cliente(Midia* m, bool shell) : m_(m), shell_(shell) {}
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> f, int code) override {
    if (!shell_ && f->IsMain()) m_->SiteCarregou(f->GetURL());
    if (f->IsMain()) Log(std::string(shell_ ? "shell" : "site") + " carregou " + f->GetURL().ToString() + " (" + std::to_string(code) + ")");
  }
  void OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> f, ErrorCode e, const CefString& txt, const CefString& url) override {
    if (!shell_ && f->IsMain() && e != ERR_ABORTED) m_->SiteCarregou(url);
    Log(std::string(shell_ ? "shell" : "site") + " ERRO " + std::to_string(e) + " " + txt.ToString() + " " + url.ToString());
  }

  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> b, CefRefPtr<CefFrame> f, CefProcessId o, CefRefPtr<CefProcessMessage> msg) override {
    return m_->roteador()->OnProcessMessageReceived(b, f, o, msg);
  }
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> b, CefRefPtr<CefFrame> f, CefRefPtr<CefRequest>, bool, bool) override {
    m_->roteador()->OnBeforeBrowse(b, f);
    return false;
  }
  void OnBeforeClose(CefRefPtr<CefBrowser> b) override { m_->roteador()->OnBeforeClose(b); }
  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString& url, const CefString&, cef_window_open_disposition_t, bool,
                     const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&, CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&,
                     bool*) override {
    if (!shell_) m_->Navegar(url);  // "abrir em nova janela": na mesma visão
    return true;                    // nunca abre janela solta
  }
  void OnTitleChange(CefRefPtr<CefBrowser>, const CefString& t) override {
    if (!shell_) m_->TituloMudou(t);
  }
  bool OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t, const CefString& msg, const CefString&, int) override {
    if (shell_) Log("shell: " + msg.ToString());
    return false;
  }

 private:
  Midia* m_;
  bool shell_;
  IMPLEMENT_REFCOUNTING(Cliente);
};

// ---------------------------------------------------------------------------------------------------------------------------- janela
void Midia::Iniciar() {
  roteador_ = CefMessageRouterBrowserSide::Create(CefMessageRouterConfig());
  roteador_->AddHandler(this, false);
  cliente_shell_ = new Cliente(this, true);
  cliente_conteudo_ = new Cliente(this, false);
  CefBrowserSettings cfg;
  cfg.background_color = 0xFF0B0B12;  // sem clarão branco antes de a interface pintar
  shell_ = CefBrowserView::CreateBrowserView(cliente_shell_, "file://" + ui_ + "/index.html", cfg, nullptr, nullptr, new DelegadoShell);
  CefBrowserSettings cfg2;
  cfg2.background_color = 0xFF000000;
  conteudo_ = CefBrowserView::CreateBrowserView(cliente_conteudo_, "about:blank", cfg2, nullptr, nullptr, nullptr);
  Log("estilos: shell " + std::to_string(shell_->GetRuntimeStyle()) + ", site " + std::to_string(conteudo_->GetRuntimeStyle()) + " (1 chrome, 2 alloy)");
  CefWindow::CreateTopLevelWindow(this);
}

void Midia::OnWindowCreated(CefRefPtr<CefWindow> w) {
  window_ = w;
  w->SetTitle("LUMNA Mídia");
  // A janela Chrome só aceita UM BrowserView Chrome (o site); a ordem importa: o site entra primeiro e a interface (Alloy) depois, e então
  // a interface desce para o fundo (índice 0), ficando por baixo do site.
  w->AddChildView(conteudo_);
  w->AddChildView(shell_);
  w->ReorderChildView(shell_, 0);
  conteudo_->SetVisible(false);
  w->SetAccelerator(kCmdMenu, 0x71 /*F2*/, false, false, false, true);
  w->SetAccelerator(kCmdInicio, 0x24 /*Home*/, false, false, true /*Alt*/, true);
  w->Show();
  if (Env("LUMNA_NAV_TELA_CHEIA") == "1") w->SetFullscreen(true);
  Aplicar();
  shell_->RequestFocus();
}

// ---------------------------------------------------------------------------------------------------------------------------- ações
// Ao abrir o menu: lê o progresso do vídeo e tira o print do site (ele ainda está em tamanho cheio, o quadro sai bonito).
void Midia::Capturar(std::function<void()> depois) {
  captura_ok_ = false;
  thumb_.clear();
  info_titulo_.clear();
  info_url_.clear();
  info_pos_ = info_dur_ = 0;
  pendentes_ = 2;
  depois_ = std::move(depois);
  int geracao = ++geracao_cap_;
  auto fim = [this, geracao]() {  // as duas respostas do DevTools chegaram (ou deram erro): segue com o que for pendente
    if (geracao != geracao_cap_ || --pendentes_ > 0 || !depois_) return;
    auto f = std::move(depois_);
    depois_ = nullptr;
    f();
  };
  if (depois_)  // se o site não responder, não trava: segue em 1,5 s com o que houver
    CefPostDelayedTask(TID_UI, new FuncTask([this, geracao]() {
      if (geracao == geracao_cap_ && depois_) { Log("captura demorou; seguindo sem ela"); auto f = std::move(depois_); depois_ = nullptr; f(); }
    }), 1500);
  auto p = CefDictionaryValue::Create();
  p->SetString("expression",
               "(()=>{const v=[...document.querySelectorAll('video')].sort((a,b)=>b.videoWidth*b.videoHeight-a.videoWidth*a.videoHeight)[0];"
               "return JSON.stringify({u:location.href,t:document.title,p:v?v.currentTime:0,d:(v&&isFinite(v.duration))?v.duration:0})})()");
  p->SetBool("returnByValue", true);
  Chamar("Runtime.evaluate", p, [this, fim](bool ok, CefRefPtr<CefDictionaryValue> d) {
    if (ok && d) {
      auto r = d->GetDictionary("result");
      auto v = r ? CefParseJSON(r->GetString("value"), JSON_PARSER_RFC) : nullptr;
      if (v && v->GetType() == VTYPE_DICTIONARY) {
        info_url_ = v->GetDictionary()->GetString("u");
        info_titulo_ = v->GetDictionary()->GetString("t");
        info_pos_ = Num(v->GetDictionary(), "p");
        info_dur_ = Num(v->GetDictionary(), "d");
      }
    }
    fim();
  });
  auto q = CefDictionaryValue::Create();
  q->SetString("format", "jpeg");
  q->SetInt("quality", 78);
  Chamar("Page.captureScreenshot", q, [this, fim](bool ok, CefRefPtr<CefDictionaryValue> d) {
    if (ok && d) {
      auto bin = CefBase64Decode(d->GetString("data"));
      if (bin && bin->GetSize()) {
        thumb_.resize(bin->GetSize());
        bin->GetData(thumb_.data(), thumb_.size(), 0);
        captura_ok_ = true;
        Log("captura " + std::to_string(thumb_.size()) + " bytes, progresso " + std::to_string(static_cast<int>(info_pos_)) + "/" +
            std::to_string(static_cast<int>(info_dur_)));
      }
    }
    fim();
  });
}

// Ao sair do serviço: pausa o player e guarda o último endereço, o progresso e o print.
// O print/título/progresso vêm da captura feita ao abrir o menu, e dizem de que endereço são (info_url_). Se o usuário trocou de serviço ANTES
// de a página nova carregar, a captura é da página antiga: ela não pode ser gravada com o nome do serviço novo (o card mostraria a imagem de outro).
void Midia::Guardar() {
  if (ativo_.empty() || !conteudo_ || !conteudo_->GetBrowser()) return;
  auto frame = conteudo_->GetBrowser()->GetMainFrame();
  frame->ExecuteJavaScript("document.querySelectorAll('video').forEach(v=>v.pause())", frame->GetURL(), 0);
  const Servico* sv = Acha(ativo_);
  bool tem_captura = !info_url_.empty();
  std::string url = tem_captura ? info_url_ : frame->GetURL().ToString();  // o endereço que ESTAVA na tela (o GetURL() já pode ser o da navegação nova)
  if (url.empty() || url == "about:blank") return;
  if (sv && !UrlDoServico(*sv, url)) {
    Log("não guardei " + ativo_ + ": a página que estava na tela (" + url + ") não é deste serviço");
    return;
  }
  Salvo& s = salvos_[ativo_];
  s.url = url;
  s.quando = static_cast<long>(time(nullptr));
  if (tem_captura) {
    if (!info_titulo_.empty()) s.titulo = info_titulo_;
    s.pos = info_pos_;
    s.dur = info_dur_;
    if (captura_ok_ && !thumb_.empty()) {
      s.thumb = "thumbs/" + ativo_ + ".jpg";
      EscreverArquivo(dados_ + "/" + s.thumb, thumb_.data(), thumb_.size());
    }
  }
  Gravar();
  Log("guardado " + ativo_ + " " + s.url);
}

// Muda o tamanho/posição do site de `de` para `para`. No modo "imagem" (padrão) o site de verdade NÃO é redimensionado a cada quadro: a interface
// (shell, por baixo do site) mostra um print no lugar dele e o anima com CSS; quando o print está pintado o site sai da frente (fica fora da
// tela, sem mudar de tamanho) e, no fim da animação (e, se `esperar_carga`, quando a página terminou de carregar), ele é colocado no tamanho final
// de uma vez só. O print vazio = um cartão com o nome do serviço (a tela de carregamento).
void Midia::Transicao(CefRect de, CefRect para, const std::string& imagem_b64, const std::string& nome, bool esperar_carga) {
  ++trans_gen_;
  fim_js_ = false;
  destino_ = para;
  if (modo_ == ModoAnim::kInstante || (modo_ == ModoAnim::kImagem && imagem_b64.empty() && !esperar_carga)) {
    ocupado_ = false;
    conteudo_->SetVisible(true);
    conteudo_->SetBounds(para);
    Log("site final " + std::to_string(para.x) + "," + std::to_string(para.y) + " " + std::to_string(para.width) + "x" + std::to_string(para.height));
    if (estado_ == Estado::kMenu) shell_->RequestFocus(); else conteudo_->RequestFocus();
    return;
  }
  if (modo_ == ModoAnim::kReal) {
    ocupado_ = false;
    conteudo_->SetVisible(true);
    conteudo_->SetBounds(de);
    Animar(de, para);
    conteudo_->RequestFocus();
    return;
  }
  ocupado_ = true;
  esperando_carga_ = esperar_carga;
  if (!esperar_carga) carregou_ = true;
  ShellJS("lumna.anim({id:" + std::to_string(trans_gen_) + ",src:" + J(imagem_b64) + ",nome:" + J(nome) + ",de:" + FracaoJSON(de) + ",para:" + FracaoJSON(para) + ",ms:320})");
  int gen = trans_gen_;  // se a interface não responder (ou a página nunca carregar), não trava: termina sozinho
  CefPostDelayedTask(TID_UI, new FuncTask([this, gen]() {
    if (gen == trans_gen_ && ocupado_) { Log("transição demorou; terminando"); carregou_ = fim_js_ = true; Finalizar(); }
  }), esperar_carga ? 12000 : 3000);
}

// Abandona uma transição em andamento (ex.: o usuário abre o menu enquanto o serviço novo ainda carrega): o site volta, cheio, e o print some.
void Midia::CancelaTransicao() {
  ++trans_gen_;
  ocupado_ = esperando_carga_ = false;
  ShellJS("lumna.animSumir()");
  conteudo_->SetBounds(Cheio());
  conteudo_->SetVisible(true);
}

void Midia::TentaFinalizar() {
  if (!ocupado_ || !fim_js_ || (esperando_carga_ && !carregou_)) return;
  Finalizar();
}

void Midia::Finalizar() {
  ocupado_ = false;
  esperando_carga_ = false;
  conteudo_->SetBounds(destino_);
  conteudo_->SetVisible(true);
  Log("site final " + std::to_string(destino_.x) + "," + std::to_string(destino_.y) + " " + std::to_string(destino_.width) + "x" + std::to_string(destino_.height));
  if (estado_ == Estado::kMenu) shell_->RequestFocus(); else conteudo_->RequestFocus();
  CefPostDelayedTask(TID_UI, new FuncTask([this]() { ShellJS("lumna.animSumir()"); }), 120);  // o print só some depois de o site pintar
}

void Midia::AbrirServico(const std::string& id, const std::string& url) {
  bool vinha_do_menu = estado_ == Estado::kMenu;
  ativo_ = id;
  Mudar(Estado::kServico);
  CefRect de = vinha_do_menu ? Encolhido() : Cheio();
  carregou_ = false;
  ignora_url_.clear();
  if (conteudo_->GetBrowser()) {
    auto frame = conteudo_->GetBrowser()->GetMainFrame();
    std::string antes = frame->GetURL();
    if (antes != url) ignora_url_ = antes;
    frame->LoadURL(url);
  } else {
    Log("aviso: a visão do site ainda não tem navegador; endereço perdido: " + url);
  }
  ShellJS("lumna.menu(false)");
  const Servico* sv = Acha(id);
  std::string img;  // a tela de carregamento: o último print deste serviço, se houver; senão um cartão com o nome
  auto it = salvos_.find(id);
  if (it != salvos_.end() && !it->second.thumb.empty()) {
    std::string bytes = LerArquivo(dados_ + "/" + it->second.thumb);
    if (!bytes.empty()) img = DataUrl(bytes.data(), bytes.size());
  }
  Transicao(de, Cheio(), img, sv ? sv->nome : id, true);
  if (sv) window_->SetTitle(sv->nome);
}

void Midia::AbrirMenu() {
  if (ocupado_ && esperando_carga_) CancelaTransicao();  // não prende o usuário numa página que demora (ou travou): o menu sempre abre
  if (estado_ != Estado::kServico || ocupado_) return;
  ocupado_ = true;  // a captura é assíncrona: ignora teclas até a transição acabar
  Mudar(Estado::kMenu);
  Capturar([this]() {
    ShellJS("lumna.menu(true," + J(ativo_) + ")");
    shell_->RequestFocus();
    Transicao(Cheio(), Encolhido(), thumb_.empty() ? "" : DataUrl(thumb_.data(), thumb_.size()), "", false);
  });
}

void Midia::FecharMenu() {
  if (estado_ != Estado::kMenu || ocupado_) return;
  Mudar(Estado::kServico);
  ShellJS("lumna.menu(false)");
  Transicao(Encolhido(), Cheio(), thumb_.empty() ? "" : DataUrl(thumb_.data(), thumb_.size()), "", false);  // o print do momento em que o menu abriu
}

void Midia::Trocar(const std::string& id) {
  if (estado_ != Estado::kMenu || ocupado_) return;
  if (id == ativo_) { FecharMenu(); return; }  // o serviço "em uso": só volta ao vídeo
  Guardar();
  const Servico* s = Acha(id);
  if (!s) return;
  auto it = salvos_.find(id);
  AbrirServico(id, it != salvos_.end() && !it->second.url.empty() ? it->second.url : s->url);  // "Retomar última página"
}

void Midia::Inicio() {
  if (estado_ == Estado::kInicio) return;
  if (estado_ == Estado::kServico) {  // (no menu a captura já foi feita ao abrir): espera o print e o progresso chegarem antes de guardar
    Capturar([this]() { IrParaInicio(); });
    return;
  }
  IrParaInicio();
}

void Midia::IrParaInicio() {
  if (estado_ == Estado::kInicio) return;
  Guardar();
  ativo_.clear();
  bool do_menu = estado_ == Estado::kMenu;
  Mudar(Estado::kInicio);
  window_->SetTitle("LUMNA Mídia");
  (void)do_menu;
  ++trans_gen_;  // cancela qualquer transição em andamento: o site some na hora e o shell mostra o início
  ocupado_ = esperando_carga_ = false;
  ShellJS("lumna.animSumir()");
  conteudo_->SetBounds(Cheio());
  conteudo_->SetVisible(false);
  ShellJS("lumna.menu(false);lumna.recarregar()");
  shell_->RequestFocus();
}

bool Midia::OnQuery(CefRefPtr<CefBrowser> b, CefRefPtr<CefFrame> f, int64_t, const CefString& pedido, bool, CefRefPtr<Callback> cb) {
  // só a NOSSA interface manda: nunca um site (a ponte nem é criada neles, mas confere de novo)
  if (!Ehshell(b) || f->GetURL().ToString().rfind("file://" + ui_, 0) != 0) return false;
  std::string r = pedido;
  Log("pedido " + r);
  if (r == "estado") { cb->Success(EstadoJSON()); return true; }
  if (r == "menu:sair") { FecharMenu(); cb->Success("ok"); return true; }
  // avisos da animação da interface; cada transição tem um número, e o aviso de uma transição já cancelada/trocada é ignorado
  if (r.rfind("anim:oculta:", 0) == 0 || r.rfind("anim:fim:", 0) == 0) {
    bool oculta = r[5] == 'o';
    int id = std::atoi(r.substr(r.rfind(':') + 1).c_str());
    if (id == trans_gen_ && ocupado_ && conteudo_) {
      if (oculta) conteudo_->SetVisible(false);  // o print já está pintado: o site sai da frente (escondido; só fora da tela, o navegador atrasa a interface)
      else { fim_js_ = true; TentaFinalizar(); }
    }
    cb->Success("ok");
    return true;
  }
  if (r == "inicio") {
    if (estado_ == Estado::kMenu || estado_ == Estado::kServico) Inicio();
    cb->Success("ok");
    return true;
  }
  if (r.rfind("trocar:", 0) == 0) {
    std::string id = r.substr(7);
    if (id == "inicio") Inicio(); else Trocar(id);
    cb->Success("ok");
    return true;
  }
  if (r.rfind("abrir:", 0) == 0 || r.rfind("retomar:", 0) == 0) {  // da tela de início
    bool retomar = r[0] == 'r';
    std::string id = r.substr(retomar ? 8 : 6);
    const Servico* s = Acha(id);
    if (!s || estado_ != Estado::kInicio) { cb->Failure(1, "servico desconhecido ou fora do inicio"); return true; }
    auto it = salvos_.find(id);
    AbrirServico(id, retomar && it != salvos_.end() && !it->second.url.empty() ? it->second.url : s->url);
    cb->Success("ok");
    return true;
  }
  cb->Failure(2, "pedido desconhecido");
  return true;
}

}  // namespace

void IniciarMidia() {
  g_midia = new Midia;
  g_midia->Iniciar();
}

}  // namespace lumna
