# Pulse Terminal: a integração com o bash. O Pulse põe este arquivo para rodar no primeiro prompt (pelo PROMPT_COMMAND que vem do ambiente); ele não precisa
# estar no ~/.bashrc de ninguém. Manda ao terminal, pelas sequências OSC, o que a tela não mostra:
#   OSC 133;A  o prompt vai aparecer          OSC 133;B  aqui se digita          OSC 133;C  o comando começou a rodar
#   OSC 133;D;N  o comando acabou com o código N
#   OSC 7770;cmd=<base64>  o comando que rodou      OSC 7770;cwd=<base64>  a pasta onde o shell está
# Um comando que não entrou no histórico (começou com espaço, ou foi só um Enter) não é avisado: o Pulse não o registra nem o manda à Emma.

[[ $- == *i* ]] || return 0
[[ -n ${__PULSE_INTEGRADO-} ]] && return 0
__PULSE_INTEGRADO=1
__pulse_arq=${BASH_SOURCE[0]}

__pulse_b64() {
	printf '%s' "$1" | base64 -w0 2>/dev/null
}

# roda a cada prompt, antes de tudo o que o PROMPT_COMMAND do usuário faz (o $? ainda é o do comando)
__pulse_precmd() {
	local ec=$?
	local h=$HISTCMD
	if [[ -n ${__pulse_h-} && $h != "$__pulse_h" ]]; then
		local c
		c=$(HISTTIMEFORMAT= builtin history 1)
		[[ $c =~ ^[[:space:]]*[0-9]+\*?[[:space:]]+(.*)$ ]] && c=${BASH_REMATCH[1]}
		printf '\e]7770;cmd=%s\a\e]7770;cwd=%s\a\e]133;D;%s\a' "$(__pulse_b64 "$c")" "$(__pulse_b64 "$PWD")" "$ec"
	elif [[ -n ${__pulse_h-} ]]; then
		printf '\e]7770;cwd=%s\a' "$(__pulse_b64 "$PWD")"
	fi
	__pulse_h=$h
	# as marcas do prompt (de novo a cada vez: o usuário pode ter mudado o PS1)
	local a='\[\e]133;A\a\]' b='\[\e]133;B\a\]'
	PS1=${PS1//"$a"/}
	PS1=${PS1//"$b"/}
	PS1="$a$PS1$b"
	return $ec
}

# o PS0 é mostrado logo antes de o comando rodar
if [[ $PS0 != *'133;C'* ]]; then
	PS0=${PS0}'\e]133;C\a'
fi

# a nossa linha sai do PROMPT_COMMAND e entra o __pulse_precmd, na frente do que mais houver
PROMPT_COMMAND=${PROMPT_COMMAND/". '$__pulse_arq'"/}
PROMPT_COMMAND=${PROMPT_COMMAND#;}
PROMPT_COMMAND="__pulse_precmd${PROMPT_COMMAND:+;$PROMPT_COMMAND}"
__pulse_h=$HISTCMD
__pulse_precmd
