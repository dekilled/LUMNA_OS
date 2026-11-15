// A comunicação do navegador com o resto do LUMNA: o serviço dos controles (lumna-gamepad), o Orbit (ponteiro virtual) e o socket de comandos
// (para o Super+M do Orbit e para o celular/Connect: `lumna-navegador --menu`).
#pragma once
#include <functional>
#include <string>

#include "include/internal/cef_types_wrappers.h"

namespace lumna {

// roda `f` na thread de interface do CEF
void NaInterface(std::function<void()> f);

std::string DirRuntime();

// ---- o serviço dos controles: $XDG_RUNTIME_DIR/lumna-gamepad.sock (LUMNA_GAMEPAD_SOCK). Uma thread assina os eventos e religa sozinha se o serviço cair.
// `botao`: "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "SELECT", "START", "GUIA", "CIMA", "BAIXO", "ESQ", "DIR"; `analogico`: a direção veio do analógico (não do direcional).
// `eixos`: os analógicos de -1 a 1 (y para baixo é positivo). Os dois são chamados na thread de interface.
using BotaoCb = std::function<void(const std::string& botao, bool pressionado, bool analogico)>;
using EixosCb = std::function<void(double lx, double ly, double rx, double ry)>;
void IniciarControle(BotaoCb botao, EixosCb eixos);

// ---- o Orbit: o ponteiro virtual (o mesmo caminho do mouse e do celular) e consultas. ORBIT_SOCK, ou o orbit.*.sock de $XDG_RUNTIME_DIR.
class OrbitIPC {
 public:
  ~OrbitIPC();
  bool Mover(double x, double y);                 // põe o ponteiro em (x, y) do layout
  bool Rolar(double delta);                       // positivo desce (unidades de rolagem; 15 = um "dente" da roda)
  bool Botao(bool baixo, bool direito = false);   // aperta/solta o botão do ponteiro
  bool Cursor(double* x, double* y);              // onde o ponteiro está (consulta, bloqueia até ~150 ms)
  bool Janela(const std::string& app_id, CefRect* r);  // posição e tamanho de uma janela pelo app_id
 private:
  bool Envia(const std::string& linha);
  std::string Pergunta(const std::string& linha);
  int fd_ = -1;
};

// ---- o socket de comandos: $XDG_RUNTIME_DIR/lumna-navegador.sock. `tratador` roda na thread do socket e devolve a resposta (uma linha).
void IniciarComandos(std::function<std::string(const std::string&)> tratador);
// o modo cliente (lumna-navegador --menu): manda o comando à instância que está rodando e imprime a resposta; devolve o código de saída
int EnviarComando(const std::string& comando);

}  // namespace lumna
