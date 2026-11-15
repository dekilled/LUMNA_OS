// Teste de viabilidade do navegador do LUMNA: abre UMA janela do CEF (Views) numa página que descobre quais codecs e o Widevine o CEF tem, e mostra o resultado no título.
#include <cstdio>
#include <cstdlib>
#include <string>
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"

class Cliente : public CefClient, public CefDisplayHandler, public CefLifeSpanHandler {
 public:
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  // Cada console.log da página sai no terminal; "FIM" encerra o teste (a menos que TESTE_MANTER=1, para olhar a janela).
  bool OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t, const CefString& msg, const CefString&, int) override {
    printf("%s\n", msg.ToString().c_str()); fflush(stdout);
    if (msg.ToString() == "FIM" && !getenv("TESTE_MANTER")) CefQuitMessageLoop();
    return true;
  }
  void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override {
    CEF_REQUIRE_UI_THREAD();
    if (auto view = CefBrowserView::GetForBrowser(browser)) {
      if (auto janela = view->GetWindow()) janela->SetTitle(title);
    }
  }
  void OnBeforeClose(CefRefPtr<CefBrowser>) override { CefQuitMessageLoop(); }
  IMPLEMENT_REFCOUNTING(Cliente);
};

class Janela : public CefWindowDelegate {
 public:
  explicit Janela(CefRefPtr<CefBrowserView> v) : view_(v) {}
  void OnWindowCreated(CefRefPtr<CefWindow> w) override {
    w->AddChildView(view_);
    w->Show();
  }
  void OnWindowDestroyed(CefRefPtr<CefWindow>) override { view_ = nullptr; }
  bool CanClose(CefRefPtr<CefWindow>) override { return true; }
  CefSize GetPreferredSize(CefRefPtr<CefView>) override { return CefSize(1000, 700); }
  IMPLEMENT_REFCOUNTING(Janela);
  DISALLOW_COPY_AND_ASSIGN(Janela);
 private:
  CefRefPtr<CefBrowserView> view_;
};

class App : public CefApp, public CefBrowserProcessHandler {
 public:
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }
  void OnBeforeCommandLineProcessing(const CefString&, CefRefPtr<CefCommandLine> cl) override {
    const char* plat = getenv("TESTE_OZONE");
    cl->AppendSwitchWithValue("ozone-platform", plat ? plat : "x11");
    if (!getenv("TESTE_GPU")) cl->AppendSwitch("disable-gpu");
    cl->AppendSwitch("no-sandbox");
    cl->AppendSwitch("no-first-run");
    cl->AppendSwitch("disable-dev-shm-usage");
    if (getenv("TESTE_LOG")) {  // log do Chromium sobre o CDM/Widevine, para saber por que ele não carrega
      cl->AppendSwitchWithValue("enable-logging", "stderr");
      cl->AppendSwitchWithValue("vmodule", "*cdm*=2,*widevine*=2,*content_decryption*=2");
    }
    if (const char* w = getenv("TESTE_WIDEVINE")) {  // caminho do libwidevinecdm.so
      cl->AppendSwitchWithValue("widevine-cdm-path", w);
      if (const char* ver = getenv("TESTE_WIDEVINE_VER")) cl->AppendSwitchWithValue("widevine-cdm-version", ver);
    }
  }
  void OnContextInitialized() override {
    CEF_REQUIRE_UI_THREAD();
    const char* url = getenv("TESTE_URL");
    CefBrowserSettings cfg;
    auto view = CefBrowserView::CreateBrowserView(new Cliente, url ? url : "about:blank", cfg, nullptr, nullptr, nullptr);
    CefWindow::CreateTopLevelWindow(new Janela(view));
  }
  IMPLEMENT_REFCOUNTING(App);
};

int main(int argc, char** argv) {
  CefMainArgs args(argc, argv);
  CefRefPtr<App> app(new App);
  int codigo = CefExecuteProcess(args, app, nullptr);
  if (codigo >= 0) return codigo;
  CefSettings s;
  s.no_sandbox = true;
  const char* cache = getenv("TESTE_CACHE");
  CefString(&s.root_cache_path) = cache ? cache : "/tmp/cef/cache";
  CefString(&s.cache_path) = cache ? cache : "/tmp/cef/cache";
  if (!CefInitialize(args, s, app, nullptr)) return 1;
  CefRunMessageLoop();
  CefShutdown();
  return 0;
}
