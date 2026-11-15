// Navegador do LUMNA: processo principal e ganchos do CEF.
#include <unistd.h>

#include <cstdlib>
#include <string>

#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/wrapper/cef_message_router.h"
#include "externo.h"
#include "midia.h"

namespace {

std::string Env(const char* nome, const char* padrao) {
  const char* v = getenv(nome);
  return (v && *v) ? v : padrao;
}

// pasta do executável (é onde ficam libcef.so, icudtl.dat, os .pak e a interface)
std::string ExeDir() {
  char b[4096];
  ssize_t n = readlink("/proc/self/exe", b, sizeof b - 1);
  if (n <= 0) return ".";
  b[n] = 0;
  std::string s = b;
  return s.substr(0, s.rfind('/'));
}

class App : public CefApp, public CefBrowserProcessHandler, public CefRenderProcessHandler {
 public:
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override { return this; }

  void OnBeforeCommandLineProcessing(const CefString&, CefRefPtr<CefCommandLine> cl) override {
    // X11 (XWayland) por padrão: é o que o Orbit entrega pronto. O Wayland nativo fica para depois (LUMNA_NAV_OZONE=wayland).
    cl->AppendSwitchWithValue("ozone-platform", Env("LUMNA_NAV_OZONE", "x11"));
    cl->AppendSwitch("no-first-run");
    cl->AppendSwitch("no-sandbox");  // TODO: ligar o sandbox (chrome-sandbox com setuid) antes de distribuir
    // nada para a Google: sem relatórios de falha, sem sincronização, sem pings
    cl->AppendSwitch("disable-breakpad");
    cl->AppendSwitch("disable-sync");
    cl->AppendSwitch("no-pings");
    if (getenv("LUMNA_NAV_SEM_GPU")) cl->AppendSwitch("disable-gpu");
    // A interface (HTML) fica por baixo do site, coberta: sem isto o Chromium a trata como "em segundo plano" e atrasa os temporizadores dela para
    // 1 s, o que deixa a troca de serviço e o menu travados/atropelados.
    cl->AppendSwitch("disable-background-timer-throttling");
    cl->AppendSwitch("disable-renderer-backgrounding");
    cl->AppendSwitch("disable-backgrounding-occluded-windows");
    // o vídeo dos serviços toca sem gesto do usuário (a TV não tem um toque antes do play)
    cl->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");
  }

  void OnContextInitialized() override { lumna::IniciarMidia(); }

  // --- processo de renderização: a ponte JS <-> C++ (window.cefQuery) só existe na NOSSA interface (file://), nunca nos sites ---
  void OnWebKitInitialized() override { roteador_ = CefMessageRouterRendererSide::Create(CefMessageRouterConfig()); }
  void OnContextCreated(CefRefPtr<CefBrowser> b, CefRefPtr<CefFrame> f, CefRefPtr<CefV8Context> c) override {
    if (f->GetURL().ToString().rfind("file://", 0) == 0) roteador_->OnContextCreated(b, f, c);
  }
  void OnContextReleased(CefRefPtr<CefBrowser> b, CefRefPtr<CefFrame> f, CefRefPtr<CefV8Context> c) override {
    roteador_->OnContextReleased(b, f, c);
  }
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> b, CefRefPtr<CefFrame> f, CefProcessId origem,
                                CefRefPtr<CefProcessMessage> m) override {
    return roteador_ && roteador_->OnProcessMessageReceived(b, f, origem, m);
  }

 private:
  CefRefPtr<CefMessageRouterRendererSide> roteador_;
  IMPLEMENT_REFCOUNTING(App);
};

}  // namespace

int main(int argc, char** argv) {
  // o modo cliente: `lumna-navegador --menu` (o Super+M do Orbit), `--inicio`, `--estado` falam com a instância que já está rodando e saem
  if (argc >= 2) {
    std::string a = argv[1];
    if (a == "--menu" || a == "--inicio" || a == "--estado") return lumna::EnviarComando(a.substr(2));
    if (a == "--salvar") return lumna::EnviarComando(argc >= 3 ? "salvar " + std::string(argv[2]) : "salvar");  // [depois|favorito|assistido]
  }
  const std::string aqui = ExeDir();
  CefMainArgs args(argc, argv);
  CefRefPtr<App> app(new App);
  int codigo = CefExecuteProcess(args, app, nullptr);
  if (codigo >= 0) return codigo;

  std::string dados = Env("LUMNA_NAV_DADOS", "");
  if (dados.empty()) {
    std::string xdg = Env("XDG_DATA_HOME", "");
    dados = xdg.empty() ? Env("HOME", "/tmp") + "/.local/share/lumna/navegador" : xdg + "/lumna/navegador";
  }
  CefSettings s;
  s.no_sandbox = true;
  CefString(&s.root_cache_path) = dados + "/perfil";
  CefString(&s.cache_path) = dados + "/perfil";
  CefString(&s.locale) = "pt-BR";
  CefString(&s.resources_dir_path) = aqui;
  CefString(&s.locales_dir_path) = aqui + "/locales";
  if (!CefInitialize(args, s, app, nullptr)) return 1;
  CefRunMessageLoop();
  CefShutdown();
  return 0;
}
