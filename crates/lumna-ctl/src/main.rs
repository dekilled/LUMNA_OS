//! `lumna-ctl launcher toggle` e afins: repassa a ordem ao lumna-shell e mostra a resposta.

use std::io::{Read, Write};
use std::os::unix::net::UnixStream;
use std::process::ExitCode;

const USAGE: &str = "uso: lumna-ctl <comando>

comandos:
  launcher toggle    abre ou fecha o launcher de apps
  launcher open
  launcher close
  mode next | prev                   troca de modo (Super + Tab)
  mode desktop | gaming | media      vai direto para um modo
  mode layout per-screen | extend    um modo por tela, ou o mesmo modo em todas
  pad up|down|left|right|accept|back|menu|guide
                                     simula o controle (testes e controle remoto)
  settings [rede|bluetooth|som|impressoras|sistema]
                                     abre as Configurações (na página pedida)
  store [descobrir|instalados|atualizacoes|fontes]
                                     abre a Loja (na aba pedida)
  files [CAMINHO]                    abre o Explorador de arquivos (numa pasta)";

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.is_empty() || args.iter().any(|a| a == "-h" || a == "--help") {
        println!("{USAGE}");
        return ExitCode::SUCCESS;
    }
    let Some(path) = lumna_core::control_socket_path() else {
        eprintln!("lumna-ctl: XDG_RUNTIME_DIR não definida");
        return ExitCode::FAILURE;
    };
    let reply = UnixStream::connect(&path).and_then(|mut stream| {
        stream.write_all(args.join(" ").as_bytes())?;
        stream.shutdown(std::net::Shutdown::Write)?;
        let mut reply = String::new();
        stream.read_to_string(&mut reply)?;
        Ok(reply)
    });
    match reply {
        Ok(reply) if reply.trim() == "ok" => ExitCode::SUCCESS,
        Ok(reply) => {
            eprintln!("lumna-ctl: {}", reply.trim());
            ExitCode::FAILURE
        }
        Err(err) => {
            eprintln!("lumna-ctl: o shell não respondeu em {}: {err}", path.display());
            ExitCode::FAILURE
        }
    }
}
