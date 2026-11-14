#!/bin/sh
# Monta a animação de abertura do LUMNA (a que toca depois do login) em UM arquivo, a partir dos três vídeos do Eric:
#   [ start ][ F: mid-loop esticado ][ R: o mesmo ao contrário ][ end ]
# O trecho do meio vai e volta ("respiração lenta"): F e R mostram os mesmos quadros em ordem oposta, então a emenda nas duas pontas é contínua
# (e dá para pular de R para o ponto igual de F sem salto). Os tempos de cada trecho vão no abertura.ini ao lado, que o tocador (lumna-abertura) lê.
# Uso: sh gera.sh [fator]    (fator = quantas vezes o mid-loop é esticado; 2 por omissão)
set -eu
cd "$(dirname "$0")"
K="${1:-2}"
dur() { ffprobe -v error -select_streams v:0 -show_entries stream=duration -of csv=p=0 "$1"; }
S=$(dur start.mp4); E=$(dur end.mp4)
# o mid-loop do Eric tem 24 quadros, mas os 2 últimos já são o começo do efeito de distorção do end (a diferença entre quadros salta de 0,2 para 4 e 26):
# o laço usa só os 22 limpos
QM=22
M=$(awk -v q="$QM" 'BEGIN { printf "%.6f", q / 30 }')
F=$(awk -v m="$M" -v k="$K" 'BEGIN { printf "%.6f", m * k }')
ffmpeg -v error -y -i start.mp4 -i mid-loop.mp4 -i end.mp4 -filter_complex "
 [0:v]fps=30,setpts=PTS-STARTPTS,format=yuv420p[sv];
 [0:a]atrim=0:$S,asetpts=PTS-STARTPTS,aresample=44100,aformat=channel_layouts=stereo[sa];
 [1:v]fps=30,trim=end_frame=$QM,setpts=$K*(PTS-STARTPTS),framerate=fps=30:interp_start=0:interp_end=255:scene=100,format=yuv420p,split[fv1][fv2];
 [1:a]atrim=0:$M,asetpts=PTS-STARTPTS,aresample=44100,aformat=channel_layouts=stereo,atempo=$(awk -v k="$K" 'BEGIN { printf "%.6f", 1 / k }'),atrim=0:$F,asetpts=PTS-STARTPTS,asplit[fa1][fa2];
 [fv2]reverse,setpts=PTS-STARTPTS[rv];
 [fa2]areverse[ra];
 [2:v]fps=30,setpts=PTS-STARTPTS,format=yuv420p[ev];
 [2:a]atrim=0:$E,asetpts=PTS-STARTPTS,aresample=44100,aformat=channel_layouts=stereo[ea];
 [sv][sa][fv1][fa1][rv][ra][ev][ea]concat=n=4:v=1:a=1[v][a]" -map '[v]' -map '[a]' \
 -c:v libx264 -preset slow -crf ${CRF:-15} ${GOP:--g 10 -bf 0} -keyint_min 1 -pix_fmt yuv420p -force_key_frames "0,$S,$(awk -v s="$S" -v f="$F" 'BEGIN{printf "%.6f", s+f}'),$(awk -v s="$S" -v f="$F" 'BEGIN{printf "%.6f", s+2*f}')" \
 -c:a aac -b:a 160k -movflags +faststart abertura.mp4
cat > abertura.ini <<EOT
# Tempos (segundos) de cada trecho de abertura.mp4; gerado por gera.sh, não edite à mão.
[abertura]
inicio = 0
frente = $(awk -v s="$S" 'BEGIN{printf "%.6f", s}')
volta = $(awk -v s="$S" -v f="$F" 'BEGIN{printf "%.6f", s+f}')
fim = $(awk -v s="$S" -v f="$F" 'BEGIN{printf "%.6f", s+2*f}')
duracao_trecho = $F
duracao_fim = $E
EOT
ls -l abertura.mp4; cat abertura.ini
