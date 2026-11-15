// Versão "Mídia" do navegador do LUMNA.
//
// Uma janela SEM moldura com duas camadas (CEF Views):
//   - "shell": a interface do LUMNA em HTML (ui/midia/), por baixo. É o início (atalhos, "Continuar assistindo") e o menu de serviços.
//   - "conteúdo": o site do serviço (YouTube, Netflix...), por cima. Só UM serviço fica vivo por vez.
// Estados:   INÍCIO (só o shell)  ->  SERVIÇO (o site cobre tudo)  <->  MENU (o site encolhe, o menu de serviços sobe, o site segue tocando).
// Trocar de serviço: guarda o último endereço, o progresso e um print do site, pausa, e o site expande já no serviço novo.
#include "midia.h"

#include "externo.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <mutex>
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
#include "include/cef_keyboard_handler.h"
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

// o estado que o comando `lumna-navegador --estado` devolve (lido da thread do socket)
std::mutex g_estado_mutex;
std::string g_estado_pub = "{\"ok\":true,\"estado\":\"inicio\",\"ativo\":\"\"}";
std::string EstadoPublico() {
  std::lock_guard<std::mutex> l(g_estado_mutex);
  return g_estado_pub;
}

// ---------------------------------------------------------------------------------------------------------------------------- a Mídia
class Midia;
CefRefPtr<Midia> g_midia;

struct Tecla { int vk; int nativo; int caractere; };  // uma tecla para mandar a uma visão (vk do Windows, código nativo do X11, caractere)
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
    if (const char* z = getenv("LUMNA_NAV_ZOOM_MENU")) { double v = atof(z); if (v >= 0.5 && v <= 1.0) zoom_menu_ = v; }
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
  // F2 (menu) e Alt+Home (início) valem em QUALQUER estado, não importa qual das duas visões tem o foco (o site escondido pode ficar com ele): por
  // isso são tratados antes de tudo, no tratamento de teclas dos dois navegadores. Devolve true se a tecla foi nossa.
  bool TeclaGlobal(const CefKeyEvent& e) {
    if (e.type != KEYEVENT_RAWKEYDOWN) return false;
    bool f2 = e.windows_key_code == 0x71, alt_home = e.windows_key_code == 0x24 && (e.modifiers & EVENTFLAG_ALT_DOWN);
    if (!f2 && !alt_home) return false;
    if (e.modifiers & EVENTFLAG_IS_REPEAT) return true;  // segurar a tecla não repete a ação
    Log(std::string("tecla global ") + (f2 ? "F2" : "Alt+Home") + " estado " + NomeEstado(estado_) + (ocupado_ ? " ocupado" : ""));
    if (f2) AlternarMenu();
    else Inicio();
    return true;
  }
  CefRefPtr<CefMessageRouterBrowserSide> roteador() { return roteador_; }
  void AlternarMenu() { if (estado_ == Estado::kMenu) FecharMenu(); else AbrirMenu(); }  // F2, Super+M (lumna-navegador --menu) e o botão Start
  void IrParaInicioPublico() { Inicio(); }
  void SiteTelaCheia(bool f) { site_fs_ = f; }
  // o controle (gamepad): botões e analógicos, na thread de interface
  void BotaoControle(const std::string& b, bool baixo, bool analogico);
  void EixosControle(double lx, double ly, double rx, double ry);
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
  bool GetLinuxWindowProperties(CefRefPtr<CefWindow>, CefLinuxWindowProperties& p) override {  // o Orbit identifica a janela por isto
    CefString(&p.wayland_app_id) = "lumna-navegador";
    CefString(&p.wm_class_class) = "lumna-navegador";
    CefString(&p.wm_class_name) = "lumna-navegador";
    return true;
  }
  cef_runtime_style_t GetWindowRuntimeStyle() override { return CEF_RUNTIME_STYLE_CHROME; }  // hospeda 1 BrowserView Chrome (o site) + os Alloy
  bool IsFrameless(CefRefPtr<CefWindow>) override { return true; }  // a Mídia desenha tudo; sem moldura nem barra do Orbit
  CefSize GetPreferredSize(CefRefPtr<CefView>) override {
    int l = std::atoi(Env("LUMNA_NAV_LARGURA", "1280").c_str()), a = std::atoi(Env("LUMNA_NAV_ALTURA", "720").c_str());
    return CefSize(l, a);
  }
  void OnWindowBoundsChanged(CefRefPtr<CefWindow>, const CefRect&) override { Aplicar(); }
  void OnLayoutChanged(CefRefPtr<CefView>, const CefRect&) override { Aplicar(); }
  bool OnAccelerator(CefRefPtr<CefWindow>, int cmd) override {
    Log("atalho " + std::to_string(cmd) + " estado " + NomeEstado(estado_) + (ocupado_ ? " ocupado" : ""));
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
    if (conteudo_ && !animando_ && !ocupado_) {
      destino_ = Alvo();
      if (estado_ == Estado::kMenu) Zoom(true);  // o zoom do menu tem tamanho fixo: acompanha a janela
      conteudo_->SetBounds(destino_);
    }
  }
  void Animar(CefRect de, CefRect para, int ms = 320) {
    anim_de_ = de; anim_para_ = para; anim_ms_ = getenv("LUMNA_NAV_SEM_ANIMACAO") ? 1 : ms;
    anim_t0_ = std::chrono::steady_clock::now();
    if (!animando_) { animando_ = true; Passo(); }
    anim_geracao_++;
  }
  void Passo();

  // --- o controle: ponteiro virtual (pelo Orbit), teclas e repetição
  void TeclaPara(CefRefPtr<CefBrowserView> v, const Tecla& t);
  void TeclaDirecao(const std::string& b);
  void AgendaTick();
  void TickControle();
  void IniciaPonteiro();
  OrbitIPC orbit_;
  double lx_ = 0, ly_ = 0, rx_ = 0, ry_ = 0;
  bool tick_ = false, pont_ativo_ = false, site_fs_ = false, a_baixo_ = false;
  double px_ = 0, py_ = 0;
  CefRect jan_;
  std::chrono::steady_clock::time_point tick_t_, jan_t_;
  struct Segurado { bool baixo = false; std::chrono::steady_clock::time_point proxima; };
  std::map<std::string, Segurado> seg_;

  // --- transição do site (ver Transicao)
  enum class ModoAnim { kImagem, kInstante, kReal };
  void Transicao(CefRect de, CefRect para, const std::string& imagem_b64, const std::string& nome, bool esperar_carga, int menu = 0);
  void TentaFinalizar();
  // Zoom da página: enquanto o menu está aberto o site fica menor, e a página se reajusta melhor com a escala um pouco reduzida (LUMNA_NAV_ZOOM_MENU,
  // padrão 0.72). É a escala de pixels do dispositivo emulada pelo DevTools (o mesmo efeito de um zoom-out de 72%): ao contrário do zoom do CEF, não mostra
  // a bolha "100%" do Chrome e não fica gravada no perfil (o zoom do CEF vale por site, e persiste).
  void Zoom(bool menu) {
    if (!conteudo_ || !conteudo_->GetBrowser()) return;
    if (Depurando()) LogPaginaDepois();  // só para o log: confirma que a escala (zoom) está mesmo aplicada na página
    if (menu && zoom_menu_ < 0.999) {
      // Os TRÊS juntos fazem um zoom-out de verdade: o viewport CSS fica MAIOR (tamanho do site / zoom), a densidade de pixels cai (zoom) e a imagem
      // é escalada (scale). Sem o "scale" a página era diagramada no viewport grande mas DESENHADA em 1:1, e estourava a janela (o botão da direita
      // cortado); só width/height/deviceScaleFactor medem certo (dpr, innerWidth) mas não escalam o que se vê.
      auto p = CefDictionaryValue::Create();
      p->SetInt("width", static_cast<int>(std::lround(destino_.width / zoom_menu_)));
      p->SetInt("height", static_cast<int>(std::lround(destino_.height / zoom_menu_)));
      p->SetDouble("deviceScaleFactor", zoom_menu_);
      p->SetBool("mobile", false);
      p->SetDouble("scale", zoom_menu_);
      Chamar("Emulation.setDeviceMetricsOverride", p, [](bool ok, CefRefPtr<CefDictionaryValue>) { if (!ok) Log("setDeviceMetricsOverride falhou"); });
    } else {
      Chamar("Emulation.clearDeviceMetricsOverride", CefDictionaryValue::Create(), [](bool, CefRefPtr<CefDictionaryValue>) {});
    }
  }
  void CancelaTransicao();
  void LogPaginaDepois();
  void CarregaPendente() {
    if (url_pendente_.empty() || !conteudo_ || !conteudo_->GetBrowser()) return;
    conteudo_->GetBrowser()->GetMainFrame()->LoadURL(url_pendente_);
    url_pendente_.clear();
  }
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
    {
      std::lock_guard<std::mutex> l(g_estado_mutex);
      g_estado_pub = std::string("{\"ok\":true,\"estado\":") + J(NomeEstado(e)) + ",\"ativo\":" + J(ativo_) + "}";
    }
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
  double zoom_menu_ = 0.72;  // escolhido pelo Eric no PC dele: serviu em Netflix, YouTube, Prime, Paramount e HBO Max
  std::string url_pendente_;  // o endereço do serviço novo: só é carregado quando o site antigo já está escondido (carregar com ele visível faz a página pular)
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
class Cliente : public CefClient, public CefLifeSpanHandler, public CefRequestHandler, public CefDisplayHandler, public CefLoadHandler, public CefFocusHandler, public CefKeyboardHandler {
 public:
  Cliente(Midia* m, bool shell) : m_(m), shell_(shell) {}
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefFocusHandler> GetFocusHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  bool OnPreKeyEvent(CefRefPtr<CefBrowser>, const CefKeyEvent& e, CefEventHandle, bool*) override { return m_->TeclaGlobal(e); }
  void OnGotFocus(CefRefPtr<CefBrowser>) override { Log(std::string("foco: ") + (shell_ ? "interface" : "site") + " GANHOU"); }
  bool OnSetFocus(CefRefPtr<CefBrowser>, FocusSource src) override {
    Log(std::string("foco: pedido para ") + (shell_ ? "interface" : "site") + (src == FOCUS_SOURCE_NAVIGATION ? " (por navegação)" : ""));
    return false;
  }
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
  void OnFullscreenModeChange(CefRefPtr<CefBrowser>, bool fs) override { if (!shell_) m_->SiteTelaCheia(fs); }
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
void Midia::Transicao(CefRect de, CefRect para, const std::string& imagem_b64, const std::string& nome, bool esperar_carga, int menu) {
  ++trans_gen_;
  fim_js_ = false;
  destino_ = para;
  auto so_menu = [&]() {  // nos modos sem animação do print, o menu sobe/desce já
    if (menu == 1) ShellJS("lumna.menu(true," + J(ativo_) + ")");
    if (menu == 2) ShellJS("lumna.menu(false)");
  };
  if (modo_ == ModoAnim::kInstante || (modo_ == ModoAnim::kImagem && imagem_b64.empty() && !esperar_carga)) {
    ocupado_ = false;
    so_menu();
    Zoom(estado_ == Estado::kMenu);
    conteudo_->SetVisible(true);
    conteudo_->SetBounds(para);
    Log("site final " + std::to_string(para.x) + "," + std::to_string(para.y) + " " + std::to_string(para.width) + "x" + std::to_string(para.height));
    if (estado_ == Estado::kMenu) shell_->RequestFocus(); else conteudo_->RequestFocus();
    return;
  }
  if (modo_ == ModoAnim::kReal) {
    ocupado_ = false;
    so_menu();
    Zoom(estado_ == Estado::kMenu);
    conteudo_->SetVisible(true);
    conteudo_->SetBounds(de);
    Animar(de, para);
    conteudo_->RequestFocus();
    return;
  }
  ocupado_ = true;
  esperando_carga_ = esperar_carga;
  if (!esperar_carga) carregou_ = true;
  ShellJS("lumna.anim({id:" + std::to_string(trans_gen_) + ",src:" + J(imagem_b64) + ",nome:" + J(nome) + ",de:" + FracaoJSON(de) + ",para:" + FracaoJSON(para) + ",ms:320,menu:" + std::to_string(menu) + ",ativo:" + J(ativo_) + "})");
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
  CarregaPendente();  // a troca de serviço já foi decidida: o endereço novo carrega mesmo assim
  Zoom(false);
  window_->ReorderChildView(shell_, 0);
  conteudo_->SetBounds(Cheio());
  conteudo_->SetVisible(true);
}

void Midia::LogPaginaDepois() {
  CefPostDelayedTask(TID_UI, new FuncTask([this]() {
    auto p = CefDictionaryValue::Create();
    p->SetString("expression", "'dpr=' + devicePixelRatio + ' largura_css=' + innerWidth");
    p->SetBool("returnByValue", true);
    Chamar("Runtime.evaluate", p, [](bool ok, CefRefPtr<CefDictionaryValue> d) {
      auto r = ok && d ? d->GetDictionary("result") : nullptr;
      if (r) Log("página: " + r->GetString("value").ToString());
    });
  }), 400);
}

void Midia::TentaFinalizar() {
  if (!ocupado_ || !fim_js_ || (esperando_carga_ && !carregou_)) return;
  Finalizar();
}

void Midia::Finalizar() {
  ocupado_ = false;
  esperando_carga_ = false;
  CarregaPendente();
  Zoom(estado_ == Estado::kMenu);
  conteudo_->SetBounds(destino_);
  conteudo_->SetVisible(true);
  window_->ReorderChildView(shell_, 0);  // a interface volta para o fundo: o site fica por cima
  Log("site final " + std::to_string(destino_.x) + "," + std::to_string(destino_.y) + " " + std::to_string(destino_.width) + "x" + std::to_string(destino_.height));
  if (estado_ == Estado::kMenu) shell_->RequestFocus(); else conteudo_->RequestFocus();
  CefPostDelayedTask(TID_UI, new FuncTask([this]() { ShellJS("lumna.animSumir()"); }), 120);  // o print só some depois de o site pintar
}

void Midia::AbrirServico(const std::string& id, const std::string& url) {
  if (modo_ == ModoAnim::kImagem) ocupado_ = true;  // ANTES de tudo: um evento de layout durante o LoadURL chamaria Aplicar() e o site antigo pularia para a tela cheia
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
    if (modo_ == ModoAnim::kImagem) url_pendente_ = url;  // carrega quando o site antigo estiver escondido (anim:oculta)
    else frame->LoadURL(url);
  } else {
    Log("aviso: a visão do site ainda não tem navegador; endereço perdido: " + url);
  }
  const Servico* sv = Acha(id);
  std::string img;  // a tela de carregamento: o último print deste serviço, se houver; senão um cartão com o nome
  auto it = salvos_.find(id);
  if (it != salvos_.end() && !it->second.thumb.empty()) {
    std::string bytes = LerArquivo(dados_ + "/" + it->second.thumb);
    if (!bytes.empty()) img = DataUrl(bytes.data(), bytes.size());
  }
  Transicao(de, Cheio(), img, sv ? sv->nome : id, true, 2);
  if (sv) window_->SetTitle(sv->nome);
}

void Midia::AbrirMenu() {
  if (ocupado_ && esperando_carga_) CancelaTransicao();  // não prende o usuário numa página que demora (ou travou): o menu sempre abre
  if (estado_ != Estado::kServico || ocupado_) return;
  ocupado_ = true;  // a captura é assíncrona: ignora teclas até a transição acabar
  ++trans_gen_;     // e invalida o alarme (watchdog) da transição anterior, que senão dispararia no meio desta e a finalizaria por engano
  Mudar(Estado::kMenu);
  Capturar([this]() {
    shell_->RequestFocus();
    Transicao(Cheio(), Encolhido(), thumb_.empty() ? "" : DataUrl(thumb_.data(), thumb_.size()), "", false, 1);
  });
}

void Midia::FecharMenu() {
  if (estado_ != Estado::kMenu || ocupado_) return;
  if (modo_ == ModoAnim::kImagem) ocupado_ = true;
  Mudar(Estado::kServico);
  Transicao(Encolhido(), Cheio(), thumb_.empty() ? "" : DataUrl(thumb_.data(), thumb_.size()), "", false, 2);  // o print do momento em que o menu abriu
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
  url_pendente_.clear();
  Zoom(false);
  window_->ReorderChildView(shell_, 0);
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
  if (r == "menu:alternar") {  // F2 com o foco na interface (menu aberto ou tela de carregamento)
    if (estado_ == Estado::kMenu) FecharMenu(); else AbrirMenu();
    cb->Success("ok");
    return true;
  }
  // avisos da animação da interface; cada transição tem um número, e o aviso de uma transição já cancelada/trocada é ignorado
  if (r.rfind("anim:oculta:", 0) == 0 || r.rfind("anim:fim:", 0) == 0) {
    bool oculta = r[5] == 'o';
    int id = std::atoi(r.substr(r.rfind(':') + 1).c_str());
    if (id == trans_gen_ && ocupado_ && conteudo_) {
      if (oculta) {
        // O site sai da frente indo para BAIXO da interface na ordem das camadas (a interface é opaca e cobre a janela toda), e fica visível e focável:
        // escondido (SetVisible(false)) o CEF não entrega teclas a nenhuma visão (o F2/Esc morreriam na tela de carregamento), e movido para fora da
        // tela (x = -30000) o CEF o recoloca na janela. Não muda de tamanho (sem relayout).
        conteudo_->SetVisible(true);  // (na primeira abertura ele ainda estava escondido)
        window_->ReorderChildView(conteudo_, 0);
        Zoom(estado_ == Estado::kMenu);  // a página se reajusta ao zoom por baixo da interface, sem aparecer
        CarregaPendente();
      }  // o print já está pintado na interface: o site pode sair da frente
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

// ---------------------------------------------------------------------------------------------------------------------------- o controle (gamepad)
// A interface (início, menu, splash) é guiada por teclas mandadas direto à visão da interface; o site, por um ponteiro virtual do Orbit (o mesmo caminho
// do mouse e do celular: o cursor aparece e funciona em qualquer site) e por teclas mandadas ao site. Só age com a janela da Mídia em foco: com um jogo
// na frente o controle é do jogo. O que não é por botão (o analógico segurado parado) é refeito aqui a 60 Hz.
Tecla TeclaDo(const std::string& nome) {
  if (nome == "CIMA") return {0x26, 111, 0};
  if (nome == "BAIXO") return {0x28, 116, 0};
  if (nome == "ESQ") return {0x25, 113, 0};
  if (nome == "DIR") return {0x27, 114, 0};
  if (nome == "Enter") return {0x0D, 36, 0};
  if (nome == "Escape") return {0x1B, 9, 0};
  if (nome == "Espaco") return {0x20, 65, ' '};
  if (nome == "PageUp") return {0x21, 112, 0};
  if (nome == "PageDown") return {0x22, 117, 0};
  if (nome == "F") return {0x46, 41, 'f'};
  return {0, 0, 0};
}

void Midia::TeclaPara(CefRefPtr<CefBrowserView> v, const Tecla& t) {
  if (!v || !v->GetBrowser() || !t.vk) return;
  auto host = v->GetBrowser()->GetHost();
  CefKeyEvent e;
  e.windows_key_code = t.vk;
  e.native_key_code = t.nativo;
  e.modifiers = 0;
  e.is_system_key = false;
  e.focus_on_editable_field = false;
  e.character = e.unmodified_character = 0;
  e.type = KEYEVENT_RAWKEYDOWN;
  host->SendKeyEvent(e);
  if (t.caractere) {
    e.type = KEYEVENT_CHAR;
    e.character = e.unmodified_character = static_cast<char16_t>(t.caractere);
    host->SendKeyEvent(e);
  }
  e.type = KEYEVENT_KEYUP;
  e.character = e.unmodified_character = 0;
  host->SendKeyEvent(e);
}

void Midia::TeclaDirecao(const std::string& b) {
  bool ui = estado_ != Estado::kServico || ocupado_;
  TeclaPara(ui ? shell_ : conteudo_, TeclaDo(b));
}

void Midia::AgendaTick() {
  if (tick_) return;
  tick_ = true;
  tick_t_ = std::chrono::steady_clock::now();
  CefPostDelayedTask(TID_UI, new FuncTask([this]() { TickControle(); }), 16);
}

void Midia::IniciaPonteiro() {
  auto agora = std::chrono::steady_clock::now();
  pont_ativo_ = true;
  if (agora - jan_t_ > std::chrono::seconds(2) || jan_.width <= 0) {
    CefRect r;
    jan_ = orbit_.Janela("lumna-navegador", &r) ? r : Cheio();
    jan_t_ = agora;
  }
  double x, y;
  bool dentro = orbit_.Cursor(&x, &y) && x >= jan_.x && x < jan_.x + jan_.width && y >= jan_.y && y < jan_.y + jan_.height;
  px_ = dentro ? x : jan_.x + jan_.width / 2.0;  // parte de onde o ponteiro de verdade está; fora da janela, do centro
  py_ = dentro ? y : jan_.y + jan_.height / 2.0;
}

void Midia::TickControle() {
  tick_ = false;
  auto agora = std::chrono::steady_clock::now();
  double dt = std::min(0.1, std::chrono::duration<double>(agora - tick_t_).count());
  bool ativo = false;
  bool em_foco = (window_ && window_->IsActive()) || getenv("LUMNA_NAV_CONTROLE_SEMPRE");
  // o direcional segurado repete (400 ms de espera, depois a cada 110 ms)
  for (auto& [b, sg] : seg_) {
    if (!sg.baixo) continue;
    ativo = true;
    if (agora >= sg.proxima) {
      sg.proxima = agora + std::chrono::milliseconds(110);
      if (em_foco) TeclaDirecao(b);
    }
  }
  bool servico = estado_ == Estado::kServico && !ocupado_ && em_foco;
  const double zona = 0.18;
  double mag = std::hypot(lx_, ly_);
  if (mag > zona) {
    ativo = true;
    if (servico) {  // o analógico esquerdo move o ponteiro (curva quadrática: fino perto do centro, rápido no fim do curso)
      if (!pont_ativo_) IniciaPonteiro();
      double k = (mag - zona) / (1 - zona);
      double vel = 1700 * k * k;
      // o ponteiro fica 12 px DENTRO da janela: nas bordas (a moldura invisível de redimensionar) a página não recebe o ponteiro e clicar viraria arrastar a janela
      const double m = 12;
      px_ = std::max<double>(jan_.x + m, std::min<double>(jan_.x + jan_.width - 1 - m, px_ + lx_ / mag * vel * dt));
      py_ = std::max<double>(jan_.y + m, std::min<double>(jan_.y + jan_.height - 1 - m, py_ + ly_ / mag * vel * dt));
      orbit_.Mover(px_, py_);
    }
  } else {
    pont_ativo_ = false;  // o ponteiro de verdade pode ter mexido (um mouse): na próxima vez parte de onde ele está
  }
  if (std::fabs(ry_) > zona) {  // o analógico direito rola a página
    ativo = true;
    if (servico) {
      // o `mouse rolar` do Orbit vale ~1 px de rolagem a cada 15 unidades (medido: 15→1 px, 300→20 px, 1000→66 px): a velocidade é em px/s e se converte
      double k = (std::fabs(ry_) - zona) / (1 - zona);
      double px_s = 1800 * k * k + 120 * k;  // fino perto do centro, rápido no fim do curso
      orbit_.Rolar((ry_ > 0 ? 1 : -1) * px_s * dt * 15.0);
    }
  }
  if (ativo) AgendaTick();
}

void Midia::EixosControle(double lx, double ly, double rx, double ry) {
  lx_ = lx; ly_ = ly; rx_ = rx; ry_ = ry;
  if (std::hypot(lx, ly) > 0.18 || std::fabs(ry) > 0.18) AgendaTick();
}

void Midia::BotaoControle(const std::string& b, bool baixo, bool analogico) {
  if (b == "SOLTAR_TUDO") {  // o serviço dos controles caiu: nada fica apertado
    seg_.clear();
    lx_ = ly_ = rx_ = ry_ = 0;
    if (a_baixo_) { orbit_.Botao(false); a_baixo_ = false; }
    return;
  }
  bool em_foco = (window_ && window_->IsActive()) || getenv("LUMNA_NAV_CONTROLE_SEMPRE");
  Log(std::string("controle ") + b + (baixo ? " baixo" : " cima") + (analogico ? " (analogico)" : "") + (em_foco ? "" : " [ignorado: sem foco]"));
  bool ui = estado_ != Estado::kServico || ocupado_;
  bool direcao = b == "CIMA" || b == "BAIXO" || b == "ESQ" || b == "DIR";
  if (direcao) {
    if (!baixo) { seg_.erase(b); return; }
    if (!em_foco) return;
    if (!ui && analogico) return;  // no site o analógico é o ponteiro (as setas vêm só do direcional)
    seg_[b] = {true, std::chrono::steady_clock::now() + std::chrono::milliseconds(400)};
    TeclaDirecao(b);
    AgendaTick();
    return;
  }
  if (!baixo) {  // soltar: só o A (o clique) tem o que desfazer
    if (b == "A" && a_baixo_) { bool ok = orbit_.Botao(false); Log(std::string("clique (botao cima) ") + (ok ? "enviado" : "FALHOU")); a_baixo_ = false; }
    return;
  }
  if (!em_foco) return;
  if (b == "START") { AlternarMenu(); return; }
  if (b == "SELECT") { Inicio(); return; }
  if (ui) {  // início, menu e transições: a interface (HTML) manda
    if (b == "A") TeclaPara(shell_, TeclaDo("Enter"));
    else if (b == "B") TeclaPara(shell_, TeclaDo("Escape"));
    return;
  }
  // um serviço aberto
  if (b == "A") { bool ok = orbit_.Botao(true); Log(std::string("clique (botao baixo) ") + (ok ? "enviado" : "FALHOU")); a_baixo_ = true; }                         // o clique (segura enquanto o botão estiver apertado: arrasta)
  else if (b == "B") {                                                            // voltar: sai da tela cheia, senão volta a página
    if (site_fs_) TeclaPara(conteudo_, TeclaDo("Escape"));
    else if (conteudo_ && conteudo_->GetBrowser() && conteudo_->GetBrowser()->CanGoBack()) conteudo_->GetBrowser()->GoBack();
  } else if (b == "X") TeclaPara(conteudo_, TeclaDo("Espaco"));                   // play/pausa
  else if (b == "Y") TeclaPara(conteudo_, TeclaDo("F"));                          // tela cheia
  else if (b == "LB") TeclaPara(conteudo_, TeclaDo("PageUp"));
  else if (b == "RB") TeclaPara(conteudo_, TeclaDo("PageDown"));
}

}  // namespace

void IniciarMidia() {
  g_midia = new Midia;
  g_midia->Iniciar();
  // `lumna-navegador --menu` (o Super+M do Orbit e o celular), `--inicio` e `--estado`
  IniciarComandos([](const std::string& cmd) -> std::string {
    if (cmd == "menu" || cmd == "alternar") { NaInterface([]() { g_midia->AlternarMenu(); }); return "{\"ok\":true}"; }
    if (cmd == "inicio") { NaInterface([]() { g_midia->IrParaInicioPublico(); }); return "{\"ok\":true}"; }
    if (cmd == "estado") return EstadoPublico();
    return "{\"ok\":false,\"erro\":\"comando desconhecido (menu, inicio, estado)\"}";
  });
  // os controles (lumna-gamepad)
  IniciarControle([](const std::string& b, bool baixo, bool an) { g_midia->BotaoControle(b, baixo, an); },
                  [](double lx, double ly, double rx, double ry) { g_midia->EixosControle(lx, ly, rx, ry); });
}

}  // namespace lumna
