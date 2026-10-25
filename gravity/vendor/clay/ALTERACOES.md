# Alterações do Gravity neste clay.h

Este `clay.h` é o do Clay (commit e6cc369) com duas correções, como pede a licença zlib:

- Os comandos de borda (`CLAY_RENDER_COMMAND_TYPE_BORDER`) e as linhas entre filhos
  (`betweenChildren`) passam a levar o `zIndex` do elemento flutuante a que pertencem, como
  todos os outros comandos. Sem isso as bordas de dentro de um flutuante saíam no z 0: no
  Gravity, que desenha cada popup na sua superfície pelo zIndex, as bordas e o anel de foco
  de dentro de um popup iam parar na janela principal. As linhas alteradas estão marcadas.

- Os comandos que fecham um recorte (`CLAY_RENDER_COMMAND_TYPE_SCISSOR_END`) passam também a levar
  o `zIndex`, como o que o abre. Sem isso, um elemento que rola dentro de um popup mandava o
  "fim do recorte" para a janela principal e o "começo" para o popup: o popup ficava com um
  restore a mais e não desenhava nada. As duas linhas estão marcadas com "Gravity".
