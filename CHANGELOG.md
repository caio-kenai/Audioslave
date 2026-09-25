# Changelog

## [1.0.2] - 2026-09-24

### Melhorias
- Janela do Audioslave: **Configurações** (laranja) no topo, ao lado do logo e da versão;
  **Verificar agora** ao lado do estado, com ícone; **Pasta de logs** embaixo à direita, no lugar
  do caminho da pasta.
- Menu de um dispositivo (botão direito) redesenhado: cabeçalho com nome, tipo e estado, largura
  fixa e ícones; *Renomear* abre um diálogo com o campo já selecionado, Enter confirma, Esc cancela
  e há uma prévia do nome como o Windows vai exibir.

### Correções
- Abrir a janela pela bandeja (ou abrir o Audioslave de novo) traz a janela para a frente dos
  outros aplicativos, inclusive na primeira abertura.

## [1.0.1] - 2026-09-24

### Novidades
- **Configurações** na janela do Audioslave (e no menu da bandeja): padronização de formato, taxa
  de amostragem, profundidade de bits e política de dispositivos incompatíveis podem ser alteradas
  a qualquer momento, sem reinstalar nem reiniciar; o serviço grava, recarrega e aplica na hora.
- Taxas de amostragem de **8000 a 384000 Hz** (8000, 11025, 12000, 16000, 22050, 24000, 32000,
  44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000); o suporte real de cada dispositivo
  continua sendo perguntado ao driver.
- Prévia antes de aplicar: dispositivos compatíveis, limitados pela profundidade de bits, sem a
  taxa de amostragem, ignorados e desabilitados; alerta de limitação de profundidade com
  *Continuar* / *Cancelar*; relatório do que não pôde ser configurado e por quê.
- Nova opção **Desabilitar dispositivos que não suportam a configuração selecionada**
  (desligada por padrão, também no instalador): desabilita os incompatíveis somente depois de
  confirmada para o formato escolhido, reativa-os quando voltam a ser compatíveis ou quando a
  opção é desligada, trata dispositivos novos e recriados e nunca entra em loop (espera de 10 min
  e no máximo 3 vezes por dia). Dispositivos desabilitados pelo usuário nunca são tocados.
- **Renomear dispositivos** com o botão direito na lista: o nome é mantido mesmo que o Windows
  ou o driver o redefinam; também é possível reativar um dispositivo desabilitado pelo Audioslave.
- Coluna de **compatibilidade** na lista de dispositivos.
- Janela com **minimizar, maximizar/restaurar e fechar**, além do redimensionamento pelas
  bordas; posição, tamanho e estado maximizado lembrados.
- Logs por dispositivo com nome, id, tipo, formato atual, pedido e suportado, resultado, motivo e
  ação; notificações na bandeja para desabilitações e reativações automáticas.
- CLI: `analyze`, `configure`, `rename` e `enable`; `devices` mostra taxas e profundidades
  suportadas; `status` mostra a política de dispositivos incompatíveis.

### Correções
- Nova logo oficial (sem as bordas laterais); ícones do aplicativo e da bandeja gerados a partir
  dela em todos os tamanhos do Windows (16 a 256 px).
- O logo do menu da bandeja só aparecia ao passar o mouse sobre ele.
- Pausar, retomar e verificar agora não fecham mais o menu da bandeja; abrir e encerrar fecham.
- O ícone da bandeja segue o protocolo de notificação atual do Windows: o menu abre ao lado do
  ícone quando o clique termina e o ícone continua visível, também entre os ícones ocultos.
- Ícone da bandeja criado pelo Windows diretamente do quadro PNG do tamanho certo, sem perda de
  nitidez, e atualizado quando a escala de DPI muda.
- A versão nos detalhes do executável acompanha a versão do projeto.
- Desinstalação silenciosa (`Uninstall.exe /S`) remove tudo, inclusive o próprio desinstalador e
  a pasta, e só retorna ao terminar.
- Janela de renomear dispositivo mais compacta.

## [1.0.0] - 2026-09-24

Primeira versão do **Audioslave**.

### Funcionalidades
- Serviço do Windows `Audioslave` (LocalSystem, início automático, recuperação automática) que
  mantém o modo exclusivo desligado em todos os dispositivos de reprodução e captura, com
  releitura de cada correção, notificações em tempo real e verificação periódica.
- Padronização opcional do formato padrão (44100 a 192000 Hz, 16/24/32 bits), aplicada somente
  quando o driver suporta o formato.
- Bandeja do sistema em JUCE com tema escuro: status em tempo real, pausar, retomar, verificar
  agora, janela de status com os dispositivos monitorados e encerrar.
- Canal de controle seguro entre bandeja/CLI e serviço (`\\.\pipe\Audioslave.Control`).
- Um único executável para bandeja, serviço e linha de comando (`status`, `pause`, `resume`,
  `rescan`, `scan`, `devices`, `diagnose`, `validate`, ...); validação do modo exclusivo com o
  WASAPI da JUCE.
- Instalador e desinstalador em JUCE, com modo silencioso e atualização no lugar; pacote portátil.
- Logs com rotação e eventos no Visualizador de Eventos.
- Ícones gerados em cada tamanho usado pelo Windows (16 a 64 px na bandeja), nítidos em qualquer
  escala de DPI.
