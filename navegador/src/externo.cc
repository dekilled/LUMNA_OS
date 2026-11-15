#include "externo.h"

#include <fcntl.h>
#include <glob.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "include/cef_parser.h"
#include "include/cef_task.h"
#include "include/cef_values.h"

namespace lumna {
namespace {

class FuncTaskExt : public CefTask {
 public:
  explicit FuncTaskExt(std::function<void()> f) : f_(std::move(f)) {}
  void Execute() override { f_(); }
 private:
  std::function<void()> f_;
  IMPLEMENT_REFCOUNTING(FuncTaskExt);
};

int ConectaUnix(const std::string& caminho) {
  if (caminho.empty() || caminho.size() >= sizeof(sockaddr_un::sun_path)) return -1;
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) return -1;
  sockaddr_un a{};
  a.sun_family = AF_UNIX;
  memcpy(a.sun_path, caminho.c_str(), caminho.size() + 1);
  if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) { close(fd); return -1; }
  return fd;
}

// o valor de "chave":"texto" / "chave":true numa linha JSON simples
std::string Campo(const std::string& l, const char* chave) {
  std::string k = std::string("\"") + chave + "\":";
  size_t i = l.find(k);
  if (i == std::string::npos) return "";
  i += k.size();
  if (i < l.size() && l[i] == '"') { size_t f = l.find('"', i + 1); return f == std::string::npos ? "" : l.substr(i + 1, f - i - 1); }
  size_t f = l.find_first_of(",}]", i);
  return l.substr(i, f == std::string::npos ? std::string::npos : f - i);
}

// "chave":[a,b]
bool Par(const std::string& l, const char* chave, double* a, double* b) {
  std::string k = std::string("\"") + chave + "\":[";
  size_t i = l.find(k);
  if (i == std::string::npos) return false;
  return sscanf(l.c_str() + i + k.size(), "%lf,%lf", a, b) == 2;
}

std::string Env(const char* n) { const char* v = getenv(n); return v ? v : ""; }

}  // namespace

void NaInterface(std::function<void()> f) { CefPostTask(TID_UI, new FuncTaskExt(std::move(f))); }

std::string DirRuntime() {
  std::string d = Env("XDG_RUNTIME_DIR");
  return d.empty() ? "/tmp" : d;
}

// ------------------------------------------------------------------------------------------------------------------------------ os controles
void IniciarControle(BotaoCb botao, EixosCb eixos) {
  std::thread([botao, eixos]() {
    std::string caminho = Env("LUMNA_GAMEPAD_SOCK");
    if (caminho.empty()) caminho = DirRuntime() + "/lumna-gamepad.sock";
    for (;;) {
      int fd = ConectaUnix(caminho);
      if (fd >= 0) {
        const char* pedido = "assinar\n";
        if (write(fd, pedido, strlen(pedido)) > 0) {
          std::string buf;
          char tmp[4096];
          bool primeira = true;  // a resposta ao "assinar" (um JSON) vem antes dos eventos
          for (ssize_t n; (n = read(fd, tmp, sizeof tmp)) > 0;) {
            buf.append(tmp, n);
            size_t nl;
            while ((nl = buf.find('\n')) != std::string::npos) {
              std::string l = buf.substr(0, nl);
              buf.erase(0, nl + 1);
              if (primeira && l.find("\"evento\"") == std::string::npos) { primeira = false; continue; }
              primeira = false;
              std::string ev = Campo(l, "evento");
              if (ev == "botao") {
                std::string b = Campo(l, "botao");
                bool p = Campo(l, "pressionado") == "true", an = Campo(l, "analogico") == "true";
                NaInterface([botao, b, p, an]() { botao(b, p, an); });
              } else if (ev == "eixo") {
                double lx = 0, ly = 0, rx = 0, ry = 0;
                if (Par(l, "esq", &lx, &ly) && Par(l, "dir", &rx, &ry)) NaInterface([eixos, lx, ly, rx, ry]() { eixos(lx, ly, rx, ry); });
              }
            }
          }
        }
        close(fd);
        NaInterface([botao]() { botao("SOLTAR_TUDO", false, false); });  // o serviço caiu: nada fica "apertado"
      }
      sleep(2);
    }
  }).detach();
}

// ------------------------------------------------------------------------------------------------------------------------------ o Orbit
namespace {
std::string CaminhoOrbit() {
  std::string s = Env("ORBIT_SOCK");
  if (!s.empty()) return s;
  std::string wd = Env("WAYLAND_DISPLAY");
  if (!wd.empty()) {
    std::string c = DirRuntime() + "/orbit." + wd + ".sock";
    if (access(c.c_str(), F_OK) == 0) return c;
  }
  glob_t g{};
  std::string r;
  if (glob((DirRuntime() + "/orbit.*.sock").c_str(), 0, nullptr, &g) == 0 && g.gl_pathc > 0) r = g.gl_pathv[0];
  globfree(&g);
  return r;
}
}  // namespace

OrbitIPC::~OrbitIPC() { if (fd_ >= 0) close(fd_); }

bool OrbitIPC::Envia(const std::string& linha) {
  if (fd_ < 0) {
    fd_ = ConectaUnix(CaminhoOrbit());
    if (fd_ < 0) return false;
    fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL) | O_NONBLOCK);
  }
  // o Orbit responde a cada linha: lê o que chegou, senão o socket dele enche e ele nos derruba
  char tmp[4096];
  while (read(fd_, tmp, sizeof tmp) > 0) {}
  std::string l = linha + "\n";
  ssize_t n = send(fd_, l.data(), l.size(), MSG_NOSIGNAL);
  if (n != static_cast<ssize_t>(l.size())) { close(fd_); fd_ = -1; return false; }
  return true;
}

std::string OrbitIPC::Pergunta(const std::string& linha) {
  int fd = ConectaUnix(CaminhoOrbit());
  if (fd < 0) return "";
  std::string l = linha + "\n", r;
  if (write(fd, l.data(), l.size()) == static_cast<ssize_t>(l.size())) {
    for (;;) {
      pollfd p{fd, POLLIN, 0};
      if (poll(&p, 1, 150) <= 0) break;
      char tmp[8192];
      ssize_t n = read(fd, tmp, sizeof tmp);
      if (n <= 0) break;
      r.append(tmp, n);
      if (r.find('\n') != std::string::npos || r.size() > (1u << 20)) break;
    }
  }
  close(fd);
  return r;
}

bool OrbitIPC::Mover(double x, double y) { char b[96]; snprintf(b, sizeof b, "mouse mover %.1f %.1f", x, y); return Envia(b); }
bool OrbitIPC::Rolar(double d) { char b[64]; snprintf(b, sizeof b, "mouse rolar %.2f", d); return Envia(b); }
bool OrbitIPC::Botao(bool baixo, bool direito) { return Envia(std::string("mouse botao ") + (baixo ? "baixo" : "cima") + (direito ? " direito" : "")); }

bool OrbitIPC::Cursor(double* x, double* y) {
  std::string r = Pergunta("cursor");
  return sscanf(strstr(r.c_str(), "\"x\":") ? strstr(r.c_str(), "\"x\":") : "", "\"x\":%lf", x) == 1 && sscanf(strstr(r.c_str(), "\"y\":") ? strstr(r.c_str(), "\"y\":") : "", "\"y\":%lf", y) == 1;
}

bool OrbitIPC::Janela(const std::string& app_id, CefRect* r) {
  std::string resp = Pergunta("janelas");
  auto v = CefParseJSON(resp, JSON_PARSER_RFC);
  if (!v || v->GetType() != VTYPE_DICTIONARY) return false;
  auto lista = v->GetDictionary()->GetList("janelas");
  if (!lista) return false;
  for (size_t i = 0; i < lista->GetSize(); i++) {
    auto j = lista->GetDictionary(i);
    if (j && j->GetString("app_id") == app_id && j->GetBool("visivel")) {
      *r = CefRect(j->GetInt("x"), j->GetInt("y"), j->GetInt("w"), j->GetInt("h"));
      return true;
    }
  }
  return false;
}

bool OrbitIPC::JanelaEmFoco(const std::string& app_id) {
  std::string resp = Pergunta("janelas");
  auto v = CefParseJSON(resp, JSON_PARSER_RFC);
  if (!v || v->GetType() != VTYPE_DICTIONARY) return false;
  auto lista = v->GetDictionary()->GetList("janelas");
  for (size_t i = 0; lista && i < lista->GetSize(); i++) {
    auto j = lista->GetDictionary(i);
    if (j && j->GetString("app_id") == app_id && j->GetBool("foco")) return true;
  }
  return false;
}

// ------------------------------------------------------------------------------------------------------------------------------ o socket de comandos
static std::string CaminhoComandos() {
  std::string s = Env("LUMNA_NAVEGADOR_SOCK");
  return s.empty() ? DirRuntime() + "/lumna-navegador.sock" : s;
}

void IniciarComandos(std::function<std::string(const std::string&)> tratador) {
  std::string caminho = CaminhoComandos();
  // se outra instância já responde nesse socket, esta não toma o lugar dela
  int teste = ConectaUnix(caminho);
  if (teste >= 0) { close(teste); fprintf(stderr, "lumna-navegador: já há uma instância respondendo em %s\n", caminho.c_str()); return; }
  unlink(caminho.c_str());
  int srv = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  sockaddr_un a{};
  a.sun_family = AF_UNIX;
  if (srv < 0 || caminho.size() >= sizeof a.sun_path) return;
  memcpy(a.sun_path, caminho.c_str(), caminho.size() + 1);
  if (bind(srv, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || listen(srv, 4) != 0) { close(srv); return; }
  chmod(caminho.c_str(), 0600);
  std::thread([srv, tratador]() {
    for (;;) {
      int c = accept4(srv, nullptr, nullptr, SOCK_CLOEXEC);
      if (c < 0) continue;
      std::string l;
      char ch;
      pollfd p{c, POLLIN, 0};
      while (l.size() < 512 && poll(&p, 1, 500) > 0 && read(c, &ch, 1) == 1 && ch != '\n') l += ch;
      std::string r = tratador(l) + "\n";
      ssize_t ignora = write(c, r.data(), r.size());
      (void)ignora;
      close(c);
    }
  }).detach();
}

int EnviarComando(const std::string& comando) {
  int fd = ConectaUnix(CaminhoComandos());
  if (fd < 0) { fprintf(stderr, "lumna-navegador: não há instância rodando (%s)\n", CaminhoComandos().c_str()); return 1; }
  std::string l = comando + "\n";
  if (write(fd, l.data(), l.size()) != static_cast<ssize_t>(l.size())) { close(fd); return 1; }
  std::string r;
  pollfd p{fd, POLLIN, 0};
  while (poll(&p, 1, 2000) > 0) {
    char tmp[1024];
    ssize_t n = read(fd, tmp, sizeof tmp);
    if (n <= 0) break;
    r.append(tmp, n);
    if (r.find('\n') != std::string::npos) break;
  }
  close(fd);
  fputs(r.c_str(), stdout);
  return r.find("\"ok\":true") != std::string::npos ? 0 : 1;
}

}  // namespace lumna
