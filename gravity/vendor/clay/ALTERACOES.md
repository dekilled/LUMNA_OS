# Alterações do Gravity neste clay.h

Este `clay.h` é o do Clay (commit e6cc369) com uma correção, como pede a licença zlib:

- Os comandos de borda (`CLAY_RENDER_COMMAND_TYPE_BORDER`) e as linhas entre filhos
  (`betweenChildren`) passam a levar o `zIndex` do elemento flutuante a que pertencem, como
  todos os outros comandos. Sem isso as bordas de dentro de um flutuante saíam no z 0: no
  Gravity, que desenha cada popup na sua superfície pelo zIndex, as bordas e o anel de foco
  de dentro de um popup iam parar na janela principal. As linhas alteradas estão marcadas.
