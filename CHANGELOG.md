# Changelog

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
