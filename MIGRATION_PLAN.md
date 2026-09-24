# Audioslave — Plano de migração (Audio Watchdog → JUCE)

> Documento vivo. Escrito **antes** da implementação, a partir da leitura integral do código do
> Audio Watchdog 1.1.0 (`caio-kenai/Audio-Watchdog`, commit `da6e35f`) e do código-fonte da
> JUCE 9.0.2. Atualizado ao final da migração com a matriz de paridade real (seção 19).

---

## 1. Estado atual

O **Audio Watchdog 1.1.0** é um programa nativo C++20/Win32 que impede que os endpoints de áudio
do Windows permitam o **modo exclusivo** e, opcionalmente, padroniza o **formato padrão**
(taxa de amostragem / profundidade de bits) de cada endpoint.

Fluxo de execução (um único executável `AudioWatchdog.exe`):

| Invocação | O que acontece |
|---|---|
| sem argumentos | Aplicação da bandeja (`RunTray`). Se o serviço existe, controla-o via SCM; senão roda o motor no próprio processo ("modo portátil"). |
| `--service` | `StartServiceCtrlDispatcherW` → `ServiceMain` → `RunCore` (motor + watcher) |
| `--foreground` | Mesmo `RunCore`, no console, Ctrl+C para parar |
| `install / uninstall / start / stop / restart / status / pause / resume / scan / devices / diagnose / version / help` | CLI de uso único |

Ciclo do motor (`WatchdogEngine::WorkerMain`, thread própria, COM MTA):

1. Verificação completa na partida.
2. Espera `WaitForMultipleObjects(stop, rescan, CheckIntervalSeconds)`.
3. Notificação de dispositivo → debounce (até 10 × 400 ms) → verificação *triggered*
   (pula endpoints que falharam há < 30 s para evitar laço notificação → escrita → notificação).
4. Timeout → verificação *full* (rede de segurança periódica).
5. Para cada endpoint (render/capture, ativos + desconectados):
   - **Exclusive Mode Protection**: lê `{B3F8FA53-0004-438E-9003-51A46E139BFC},3/4`
     (`VT_UI4`; `VT_EMPTY` = padrão do Windows = *permitido*). Se permitido e `Enforce=true`,
     grava `0/0`, `Commit()`, relê e confirma.
   - **Audio Format Standardization** (endpoints ativos): lê `PKEY_AudioEngine_DeviceFormat`;
     se diferente do alvo, pergunta ao driver via `IKsFormatSupport` (pino Software_IO) quais
     layouts são suportados (24 bits = 24/24 ou 24-em-32; 32 bits = int ou float); aplica com
     `IPolicyConfig::SetDeviceFormat`; relê e confirma. Nunca troca por outro formato.
6. Resumo no log; correções também no Log de Eventos.

Fatos verificados em campo (não re-derivar — vêm do histórico do Audio Watchdog):

- Com o modo exclusivo bloqueado, `IAudioClient::IsFormatSupported(EXCLUSIVE)` retorna
  `AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED (0x8889000E)` → a capacidade de formato **tem** de vir
  de `IKsFormatSupport`.
- Gravar `PKEY_AudioEngine_DeviceFormat` via `IPropertyStore` **não** muda o mix format do motor
  de áudio; `IPolicyConfig::SetDeviceFormat` muda, inclusive a partir do serviço LocalSystem.
- Propriedade de modo exclusivo ausente = permitido (endpoints NDI).

## 2. Arquitetura atual

```
AudioWatchdog.exe (Win32 GUI subsystem)
├── main.cpp ............ roteia bandeja / serviço / CLI
├── app/TrayApp ......... Shell_NotifyIcon, menu Win32, diálogo de status (.rc),
│                         Backend = ServiceBackend (SCM) | LocalBackend (motor in-process)
├── service/AudioWatchdogService .. SCM (ServiceMain, HandlerEx, pausa/continuação,
│                                   PARAMCHANGE), RunCore, RunForeground
├── core/WatchdogEngine . estado RUNNING/PAUSED/STOPPING/STOPPED, varreduras, debounce,
│                         backoff, NoticeOnce
├── audio/
│   ├── AudioTypes ...... EndpointInfo, AudioFormat, IAudioDeviceLister,
│   │                     IExclusiveModeStore, IAudioFormatStore
│   ├── CoreAudioDeviceLister .. IMMDeviceEnumerator
│   ├── AudioDeviceWatcher ..... IMMNotificationClient (objeto "não possuído pelo COM")
│   ├── ExclusiveModeManager / ExclusiveModeStore .. política + IPropertyStore
│   └── FormatManager / AudioFormatStore ........... política + IKsFormatSupport/IPolicyConfig
├── config/Config ....... INI em ProgramData (std::wifstream)
├── logging/Logger, EventLog .. arquivo com rotação 5 MB × 5, ReportEventW
├── cli/Cli, Diagnostic . comandos, inventário, sonda WASAPI exclusiva
└── util/ ............... ComPtr, ComInitializer, StrUtil, Paths (+ACLs SDDL), ServiceUtil (SCM)
AudioWatchdog-Setup.exe . instalador Win32 (payload RCDATA, assistente .rc, Uninstall.exe)
tests/ .................. harness próprio (44 testes) + mocks das 3 interfaces de áudio
```

Dependências externas: nenhuma (só SDK do Windows). Build: CMake ≥ 3.20 + Ninja/VS 2022.

### Achados da auditoria de qualidade (Audio Watchdog)

| # | Achado | Impacto | Tratamento no Audioslave |
|---|---|---|---|
| A1 | `AudioDeviceWatcher::Notify` verifica `delivering_` e depois chama `onChanged_` **sem trava**: um callback que passou do teste pode ainda estar executando depois que `Stop()` retorna → callback após destruição do motor. | use-after-free raro no shutdown | Sink protegido por `CriticalSection`; `stop()` zera o sink sob a trava antes de `Unregister`. |
| A2 | Objeto COM do watcher com `Release()` que nunca apaga (vida útil gerida pelo dono) — funciona, mas é frágil. | lifetime | Objeto COM real, no heap, contagem atômica, `ComSmartPtr`. |
| A3 | `Pause()` só troca o estado; uma escrita já iniciada no meio da varredura ainda pode acontecer depois que a pausa "retornou". | 1 escrita após pausar | Trava de modificação: `pause()` só retorna quando nenhuma alteração está em andamento. |
| A4 | Vários `HRESULT` ignorados: `ChangeServiceConfig2W` (descrição, recuperação), `SetServiceObjectSecurity`, `RegisterEndpointNotificationCallback` no uninstall, `SetNamedSecurityInfo` sem log. | falhas silenciosas | Retornos verificados e registrados no log / setup.log. |
| A5 | `SC_HANDLE`, `HKEY`, `HANDLE`, `PROPVARIANT`, `CoTaskMem` liberados manualmente em cada caminho. | vazamento em caminhos de erro | Wrappers RAII (`ScHandle`, `RegKey`, `UniqueHandle`, `PropVariant`, `CoTaskMemPtr`). |
| A6 | Bandeja: `while (g_opsInFlight > 0) Sleep(50)` e `std::wstring*` via `PostMessage`. | acoplamento/ownership manual | JUCE `MessageManager::callAsync` + objetos com dono claro. |
| A7 | Config: `SampleRate` limitado a 44100/48000. | requisito novo | Aceita 44100/48000/88200/96000/176400/192000 (continua nunca forçando formato não suportado). |
| A8 | Taxas/profundidades inválidas no INI registradas via logger antes de o logger estar configurado. | aviso perdido | `Configuration::load` devolve a lista de avisos; o chamador registra depois de configurar o log. |

Nenhum vazamento de memória, deadlock ou thread órfã foi encontrado no caminho normal; os itens
acima são os pontos que a migração corrige.

## 3. Mapeamento para JUCE

A JUCE 9.0.2 foi analisada **pelo código-fonte** (não por suposição). Resumo dos pontos
decisivos:

- `WASAPIAudioIODeviceType` (juce_audio_devices) enumera **apenas endpoints ativos**
  (`DEVICE_STATE_ACTIVE`), expõe **nomes** (com sufixos de duplicidade), não expõe os
  IDs de endpoint, e só chama os listeners quando a **lista** muda. Uma mudança de propriedade
  (Windows religando o modo exclusivo) **não gera callback JUCE**.
- O modo WASAPI exclusivo da JUCE descobre formatos com `IAudioClient::IsFormatSupported
  (EXCLUSIVE)`, que falha com `0x8889000E` justamente quando o Audioslave bloqueou o modo
  exclusivo. Não serve para descobrir capacidade de formato; serve como **sonda comportamental**
  (se `open()` falhar, o bloqueio está valendo — exatamente o caminho que um player JUCE usaria).
- A JUCE não tem API para: property store de endpoint, `IPolicyConfig`, `IKsFormatSupport`,
  SCM, Event Log.
- `juce::NamedPipe` cria o pipe com **descritor de segurança nulo**: criado por LocalSystem,
  usuários comuns só teriam leitura (a bandeja não conseguiria enviar comandos) e clientes
  remotos não são rejeitados. O **cliente** (`InterprocessConnection::connectToPipe`) e o
  **enquadramento** (magic + tamanho, little-endian) são reutilizáveis.
- `JUCEApplication` single-instance usa mutex `Global\` + broadcast por `EnumWindows` da sessão
  atual → uma segunda sessão (RDP) nunca teria bandeja. O Audio Watchdog é por sessão.
- `SystemTrayIconComponent` (Windows) re-adiciona o ícone em `TaskbarCreated`, tem tooltip e
  eventos de mouse: substitui a implementação manual.
- `ComBaseClassHelperBase` usa contagem de referência **não atômica**; `ComSmartPtr` é público
  e adequado para *consumir* interfaces COM.
- A JUCE 9 compila zlib/libpng como **C** → o projeto precisa de `LANGUAGES C CXX`.
- `JUCE_USER_DEFINED_RC_FILE` permite manter o `.rc` próprio (logo.ico original, VERSIONINFO).

| Componente atual | Responsabilidade | API atual | Alternativa JUCE | Decisão | Motivo |
|---|---|---|---|---|---|
| `TrayApp` (ícone) | Ícone, tooltip, ícone de pausa, recriação após Explorer reiniciar | `Shell_NotifyIcon`, janela oculta | `SystemTrayIconComponent` | **MIGRAR PARA JUCE** | Equivalente completo, inclusive `TaskbarCreated`. |
| `TrayApp` (menu) | Menu de contexto | `TrackPopupMenuEx` | `PopupMenu::showMenuAsync` | **MIGRAR PARA JUCE** | Equivalente. |
| `TrayApp` (janela "Abrir") | Janela de status | `CreateDialogParam` + `.rc` | `DocumentWindow` + `Component` | **REESCREVER (JUCE)** | Mais informação (endpoints, última varredura), sem `.rc` de diálogo. |
| `TrayApp` (loop, ciclo de vida) | Mensagens, encerramento de sessão | `GetMessage`, `WM_ENDSESSION` | `JUCEApplication` / `MessageManager` | **MIGRAR PARA JUCE** | JUCE já trata `WM_ENDSESSION` → `systemRequestedQuit`. |
| `TrayApp` (instância única) | 1 bandeja por sessão, 2ª abre status | `CreateMutex(Local\)` + `FindWindow` | `moreThanOneInstanceAllowed` | **WINDOWS NATIVE** (`SessionInstance`) | JUCE é por máquina (`Global\`), quebraria RDP/multiusuário. |
| `TrayApp` Backend SCM/Local | Controlar serviço / motor portátil | SCM + motor in-process | `InterprocessConnection` | **REESCREVER** | Bandeja vira cliente IPC puro; motor nunca duplicado (ver §7). |
| `ServiceUtil` | Instalar/remover/iniciar/parar/pausar serviço, recuperação, DACL | SCM | — | **WINDOWS NATIVE** (`ServiceController`) | Sem equivalente; RAII + HRESULTs verificados. |
| `AudioWatchdogService` | `ServiceMain`, handler, estados SCM | SCM | — | **WINDOWS NATIVE** (`WindowsService`) + `ServiceHost` | Sem equivalente. Núcleo do host separado do SCM (testável). |
| — (novo) | Canal bandeja/CLI ↔ serviço | — | `InterprocessConnection` (cliente) | **JUCE (cliente) + WINDOWS NATIVE (servidor)** | Servidor precisa de DACL, `PIPE_REJECT_REMOTE_CLIENTS`, várias instâncias. Mesmo protocolo de quadros da JUCE. |
| `WatchdogEngine` (thread, eventos) | Worker, stop/rescan, timeout | `CreateThread`, `WaitForMultipleObjects` | `juce::Thread` (`wait`/`notify`/`signalThreadShouldExit`) | **MIGRAR PARA JUCE** | Simplifica; sem `TerminateThread` (nunca `stopThread` com timeout). |
| `WatchdogEngine` (sincronização) | config, memo, estado | `std::mutex`, `std::atomic` | `CriticalSection`, `ThreadSafeListenerList` | **ADAPTAR** | Listeners para status push via IPC. |
| `ExclusiveModeManager` | Política do modo exclusivo | lógica pura | — | **MANTER** (`core/ExclusiveModePolicy`) | Lógica testada; só renomeada/organizada. |
| `FormatManager` | Política de formato | lógica pura | — | **MANTER** (`core/FormatPolicy`) | Idem. |
| `CoreAudioDeviceLister` | Enumeração com IDs, estado, default, inclui desconectados | MMDevice | `AudioIODeviceType::getDeviceNames` | **WINDOWS NATIVE** (`WindowsAudioEndpointEnumerator`) | JUCE não expõe IDs nem endpoints desconectados. |
| `AudioDeviceWatcher` | Adição/remoção/estado/propriedade | `IMMNotificationClient` | `AudioIODeviceType::Listener` | **WINDOWS NATIVE** (`WindowsAudioDeviceWatcher`) | Listener JUCE não dispara em mudança de propriedade. Corrige A1/A2. |
| `ExclusiveModeStore` | Ler/gravar política exclusiva | `IPropertyStore` | — | **WINDOWS NATIVE** (`WindowsExclusiveModePolicy`) | Sem equivalente. |
| `AudioFormatStore` | Formato atual / suportado / aplicar | `IPropertyStore`, `IKsFormatSupport`, `IPolicyConfig` | `AudioIODevice::getAvailableSampleRates` | **WINDOWS NATIVE** (`WindowsAudioFormatPolicy` + `WindowsFormatSupport`) | JUCE depende do modo exclusivo, que está bloqueado. |
| Sonda WASAPI exclusiva (`diagnose --probe-exclusive`) | Validação comportamental | `IAudioClient::Initialize(EXCLUSIVE)` | `WASAPIDeviceMode::exclusive` + `AudioIODevice::open` | **MIGRAR PARA JUCE** (`JuceExclusiveModeProbe`) | É o caminho real de um player JUCE (como os da Playlist). |
| — (novo) | Inventário "como a JUCE vê" | — | `AudioIODeviceType` WASAPI shared | **JUCE** (`JuceAudioDeviceInventory`) | Mostra os nomes que um player JUCE usa para escolher o dispositivo. |
| `Config` | INI | `std::wifstream` | `juce::File`, `StringArray`, `String` | **ADAPTAR (JUCE)** | Mantém o formato INI editável. `PropertiesFile` (XML) quebraria compatibilidade e legibilidade. |
| `Logger` | Arquivo + rotação 5 MB × 5 | `_wfsopen`, `MoveFileW` | `juce::Logger`, `File`, `FileOutputStream`, `Time` | **ADAPTAR (JUCE)** | Subclasse de `juce::Logger`: mensagens internas da JUCE caem no mesmo log. `FileLogger` não rotaciona durante a execução. |
| `EventLog` | Log de Eventos | `ReportEventW` | — | **WINDOWS NATIVE** | Sem equivalente. |
| `Paths` | Pastas + ACLs | `SHGetFolderPath`, SDDL | `File::getSpecialLocation` | **ADAPTAR** (JUCE para caminhos, nativo para ACL) | ACL não existe na JUCE. |
| `StrUtil` | Conversões, trim, formato | Win32 | `juce::String` | **REMOVER** (quase todo) | `juce::String` cobre; só `HrText`/`FormatSystemError` ficam (`WinError`). |
| `ComPtr`, `ComInitializer` | RAII COM | próprio | `juce::ComSmartPtr` | **MIGRAR PARA JUCE** (`ComSmartPtr`) + `ScopedComInit` nativo | JUCE não inicializa COM por thread. |
| `Cli` / `Diagnostic` | Comandos | `std::wcout` | `juce::ArgumentList`, `ConsoleApplication` | **ADAPTAR (JUCE)** | Parsing e ajuda pela JUCE; comandos iguais + novos (`rescan`). |
| Testes (harness próprio) | 44 testes | próprio | `juce::UnitTest` / `UnitTestRunner` | **MIGRAR PARA JUCE** | Framework padrão da JUCE; categorias; mesmos mocks. |
| Instalador | Assistente, payload, serviço, atalhos, Apps & Features, Uninstall.exe | Win32 dialog `.rc`, RCDATA | `JUCEApplication`, `Component`s, `ZipFile`, `FileChooser` | **REESCREVER UI (JUCE) + WINDOWS NATIVE (sistema)** | Interface e extração do pacote em JUCE; SCM, registro, atalhos, ACL, auto-remoção continuam nativos. |
| Ícones/logo | Identidade | `.ico` + `.png` | `juce_add_binary_data` (PNG da bandeja) | **MANTER** | Mesma logo; PNGs extraídos dos `.ico` originais. |

## 4. Componentes que permanecerão Windows Native

| Componente | Por quê |
|---|---|
| **MMDevice / `IMMDeviceEnumerator`** | Única fonte com ID estável do endpoint, estado (`ACTIVE`/`UNPLUGGED`), fluxo e default. A JUCE só lista ativos, por nome. |
| **`IMMNotificationClient`** | Único mecanismo que avisa mudança de *propriedade* (Windows/driver religando o modo exclusivo, formato alterado no painel de Som). |
| **`IPropertyStore`** (chaves `{B3F8FA53…},3/4` e `PKEY_AudioEngine_DeviceFormat`) | A política de modo exclusivo *é* essa propriedade; não há outra API. |
| **`IKsFormatSupport`** via `IDeviceTopology` | Único jeito de saber o que o driver suporta com o modo exclusivo bloqueado. |
| **`IPolicyConfig`** | Única forma de aplicar o formato padrão que o motor de áudio respeita imediatamente. |
| **SCM** (`ServiceMain`, `SetServiceStatus`, `CreateService`, recuperação, DACL do serviço) | Serviço do Windows não existe na JUCE. |
| **Event Log** | Idem. |
| **Servidor de named pipe** | Precisa de DACL (SYSTEM, Administradores, Usuários Interativos), `PIPE_REJECT_REMOTE_CLIENTS`, `FILE_FLAG_FIRST_PIPE_INSTANCE` e várias instâncias simultâneas. |
| **Instância por sessão da bandeja** (`Local\` mutex/eventos) | JUCE é global por máquina. |
| **Job object do host portátil** | Garante que o host portátil morre junto com a bandeja. |
| **ACLs de pasta, registro, atalhos `IShellLink`, lançamento não elevado, auto-remoção do desinstalador** | Integração com o SO sem equivalente JUCE. |

Todas essas chamadas ficam **centralizadas** em `src/platform/windows/`, `src/audio/windows/`,
`src/service/` e `src/ipc/PipeServer*`. Nenhum outro diretório inclui `<windows.h>`.

## 5. Nova arquitetura

```
                 ┌───────────────────────────── AudioslaveService.exe ─────────────────────────────┐
 SCM ──control──▶│ WindowsService (SCM)          ServiceHost ◀── console (run) / portable (--portable)│
                 │        │ report state              │                                             │
                 │        └──────────────▶ ServiceHost ── owns ──┬─ WatchdogEngine (juce::Thread)    │
                 │                                                │     ├─ ExclusiveModePolicy        │
                 │                                                │     ├─ FormatPolicy               │
                 │                                                │     └─ EndpointMemory (backoff)   │
                 │                                                ├─ WindowsAudioDeviceWatcher ──────┼─ IMMNotificationClient
                 │                                                ├─ PipeServer (native, secure) ◀───┼──┐
                 │                                                └─ ControlDispatcher (commands)    │  │
                 │  audio/windows: Enumerator · ExclusiveModePolicy · AudioFormatPolicy · FormatSupport│  │
                 │  audio/juce:    JuceExclusiveModeProbe · JuceAudioDeviceInventory (CLI diagnose)   │  │
                 │  cli: install · status · pause · resume · scan · rescan · devices · diagnose …     │  │
                 └──────────────────────────────────────────────────────────────────────────────────┘  │
                                                                                     \\.\pipe\Audioslave.Control
                 ┌───────────────────────────── Audioslave.exe (JUCE GUI) ──────────────────────┐      │
                 │ AudioslaveApplication (JUCEApplication)                                         │      │
                 │  ├─ SessionInstance (Local\ mutex, eventos Show/Quit)                           │      │
                 │  ├─ TrayIcon (SystemTrayIconComponent + PopupMenu)                              │      │
                 │  ├─ StatusWindow (DocumentWindow)                                               │      │
                 │  ├─ TrayController (estado → menu/ícone; testável sem GUI)                      │      │
                 │  ├─ ControlClient (juce::InterprocessConnection) ───────────────────────────────┼──────┘
                 │  └─ PortableHost (lança AudioslaveService.exe --portable em Job object)         │
                 └──────────────────────────────────────────────────────────────────────────────────┘
```

Estrutura de código:

```
src/
  common/        Version, Strings (juce::String ↔ wide), Result helpers
  platform/windows/  WinHandles (RAII), WinError, ScopedComInit, Paths (+ACL), EventLog,
                 SessionInstance, Elevation, JobObject
  config/        Configuration
  logging/       Logger (juce::Logger)
  audio/models/  AudioEndpoint, AudioFormat, DeviceChange
  audio/         AudioInterfaces (IAudioEndpointEnumerator, IExclusiveModeStore, IAudioFormatStore)
  audio/windows/ WindowsAudioEndpointEnumerator, WindowsAudioDeviceWatcher,
                 WindowsExclusiveModePolicy, WindowsAudioFormatPolicy, WindowsFormatSupport, ComHelpers
  audio/juce/    JuceExclusiveModeProbe, JuceAudioDeviceInventory
  core/          WatchdogEngine, ExclusiveModePolicy, FormatPolicy, EndpointMemory, EngineStatus
  ipc/           Protocol (JSON + quadros JUCE), PipeServer (Windows), ControlClient (JUCE)
  service/       ServiceHost, WindowsService, ServiceController
  cli/           Commands, Diagnostics
  app/           AudioslaveApplication, TrayIcon, TrayController, StatusWindow, PortableHost
  installer/     SetupApplication, SetupWizard, InstallerCore (nativo), Payload (ZipFile)
tests/           juce::UnitTest (unit) + testes de integração Windows somente leitura
```

## 6. Estratégia de Service

- Executável próprio **`AudioslaveService.exe`** (subsistema console; o SCM não cria janela).
  Motivo: `START_JUCE_APPLICATION` da bandeja é dono do `WinMain`; o serviço não deve carregar
  módulos de GUI; superfície menor na sessão 0.
- Nome `Audioslave`, display `Audioslave`, descrição
  *"Monitors Windows audio devices and prevents applications from using exclusive audio mode."*
- LocalSystem, início automático, recuperação reiniciar em 5 s / 10 s / 30 s, reset 1 dia,
  `FailureActionsOnNonCrashFailures`; DACL com start/stop/pause-continue para Usuários
  Interativos (a bandeja e `pause/resume` sem administrador).
- `WindowsService` só traduz SCM ↔ `ServiceHost`. `ServiceHost` possui motor, watcher, servidor
  IPC e despachante de comandos, e é o **mesmo** objeto usado por `run` (console) e `--portable`.
- Pausa/continuação/parada/`PARAMCHANGE` chegam do SCM **ou** do IPC e passam pelo mesmo caminho
  (`ServiceHost::pause/resume/stop/reload`), que também reporta o estado ao SCM → `services.msc`
  sempre coerente.
- Parada pedida pela bandeja (IPC `STOP`) termina com código 0 → recuperação não dispara
  (mesma semântica do "Encerrar" atual).
- Funciona sem usuário logado; não depende da bandeja.

## 7. Estratégia de Tray

- `Audioslave.exe` = `JUCEApplication` sem janela principal; `SystemTrayIconComponent` com a
  logo (PNG extraído do `logo.ico` original) e a versão cinza (`logo-paused.ico`) quando
  pausado/parado.
- Tooltip fixo: **"O Audioslave está em execução"**.
- Menu: `Audioslave` (título, abre status) · `Status: …` · `Pausar monitoramento` ·
  `Retomar monitoramento` · `Verificar agora` · `Abrir` · `Encerrar`.
- Clique esquerdo = Abrir. Segunda execução na mesma sessão = Abrir (evento `Local\`).
- A bandeja **não** executa o motor. Com o serviço instalado, é cliente IPC; sem serviço
  (modo portátil) lança `AudioslaveService.exe --portable` num Job object
  (`KILL_ON_JOB_CLOSE`) e se conecta a ele pelo mesmo pipe → nenhuma lógica duplicada e nenhum
  estado órfão se a bandeja fechar ou travar.
- Estado vem por **push** do serviço (eventos IPC) + reconexão a cada 2 s; sem conexão consulta o
  SCM (Parado / Não instalado). Serviço desinstalado → bandeja fecha (como hoje).
- Retomar com o serviço parado → `StartService` (DACL permite). Encerrar → confirmação → IPC
  `STOP` → fecha a bandeja. Fechar a bandeja (logoff, instalador) **não** para o serviço.
- `TrayController` (lógica pura) separado da GUI para testes.

## 8. Estratégia de IPC

- Pipe `\\.\pipe\Audioslave.Control`. Servidor nativo (`PipeServer`): SDDL
  `D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;IU)` (+ dono no modo portátil),
  `PIPE_REJECT_REMOTE_CLIENTS`, `FILE_FLAG_FIRST_PIPE_INSTANCE` na primeira instância, I/O
  sobreposto com evento de parada, até 16 clientes, limite de 64 KB por mensagem.
- Quadros idênticos aos da `juce::InterprocessConnection` (magic `0x41534C56` + tamanho, LE),
  então o cliente é `juce::InterprocessConnection` puro (thread de leitura, callbacks no message
  thread na bandeja, fora dele no CLI).
- Payload JSON (`juce::JSON`), versão de protocolo `1`:
  - requisição `{"proto":1,"id":7,"cmd":"STATUS|PAUSE|RESUME|SCAN|STOP|RELOAD"}`
  - resposta `{"proto":1,"id":7,"ok":true,"status":{…}}` ou `{"ok":false,"error":"…"}`
  - evento `{"proto":1,"event":"status","status":{…}}` a cada mudança de estado/varredura.
- `status` = estado, versão, pid, modo (serviço/portátil), recursos, alvo de formato, última
  varredura (hora + contadores), totais desde o início, endpoints (nome, fluxo, estado,
  exclusivo, formato), pasta de logs.

## 9. Estratégia de áudio

- Enumeração, watcher, políticas: nativos (§4), atrás das interfaces
  `IAudioEndpointEnumerator`, `IExclusiveModeStore`, `IAudioFormatStore` (mockáveis).
- COM: `juce::ComSmartPtr` para consumo; `ScopedComInit` (MTA) por thread que usa COM (motor,
  host, CLI); `PropVariant` e `CoTaskMemPtr` RAII.
- JUCE audio:
  - `JuceExclusiveModeProbe`: abre o endpoint com o tipo **"Windows Audio (Exclusive Mode)"**
    da JUCE; falha ao abrir = bloqueio confirmado do ponto de vista de um app JUCE.
    Usada em `diagnose --probe-exclusive` e `validate` (sob demanda; nunca periódica,
    para não tomar o dispositivo de um app em produção).
  - `JuceAudioDeviceInventory`: lista os dispositivos WASAPI como a JUCE os nomeia (para
    configurar players JUCE) em `devices`.

## 10. Estratégia de Exclusive Mode

Idêntica em comportamento, reorganizada:

1. `WindowsAudioEndpointEnumerator` lista render+capture ativos e desconectados.
2. `ExclusiveModePolicy::judgeAndFix` lê (`VT_EMPTY` = permitido), grava `0/0` só se
   `Enforce` e fora de backoff e não pausado, e **relê** para confirmar.
3. Resultados: `AlreadyOff`, `Fixed`, `Skipped`, `Unknown`, `WriteFailed`, `VerifyFailed`.
4. Reação: `IMMNotificationClient` (adicionado/removido/estado/default/propriedade) →
   debounce → varredura; periódica como rede de segurança; retomada = varredura completa.
5. Endpoint recriado/reconectado → memo de falhas/avisos daquele endpoint é limpo.
6. Log (+ Event Log) de cada correção; falha de leitura avisada uma vez por endpoint.
7. Validação comportamental opcional via JUCE (`validate`).

## 11. Estratégia de Audio Format

Mantida (`FormatPolicy` + `WindowsAudioFormatPolicy`): lê formato atual → compara → pergunta ao
driver (`IKsFormatSupport`) cada layout candidato → aplica com `IPolicyConfig` → relê → registra.
Não suportado = pula e registra os formatos suportados (uma vez por endpoint). Taxas permitidas
passam a ser 44100/48000/88200/96000/176400/192000; profundidades 16/24/32. A JUCE não assume
nenhuma parte (ver §3).

## 12. Estratégia de CMake

- `cmake_minimum_required(3.22)` (exigência da JUCE 9), `project(Audioslave LANGUAGES C CXX RC)`.
- JUCE como **submódulo git** em `external/JUCE`, fixado na tag **9.0.2**
  (`.gitmodules` com `shallow = true`). Motivos: reprodutível (commit exato no repositório),
  funciona offline depois do clone, abre direto no Visual Studio, CI só precisa
  `--recurse-submodules`. `-DAUDIOSLAVE_JUCE_DIR=<path>` permite usar uma cópia compartilhada
  da JUCE (ex.: a da Playlist). Erro claro se o submódulo não foi inicializado.
- Uma biblioteca estática `audioslave_juce` compila os módulos JUCE **uma vez**
  (link `PRIVATE`, como a documentação da JUCE exige) e reexporta includes/definições.
  `audioslave_core` (nosso código) → `AudioslaveService`, `Audioslave`, `AudioslaveSetup`,
  `audioslave_tests`.
- Flags JUCE: `JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0`, `JUCE_WASAPI=1`, `JUCE_DIRECTSOUND=0`,
  `JUCE_ASIO=0`, `JUCE_USE_WINDOWS_MEDIA_FORMAT=0`, `JUCE_STRICT_REFCOUNTEDPOINTER=1`,
  `JUCE_USER_DEFINED_RC_FILE` para usar nossos `.rc`.
- CRT estático (`/MT`), `/W4 /utf-8 /permissive- /EHsc /sdl` no nosso código.
- `cmake -S . -B build` + `cmake --build build --config Release` funciona com o gerador padrão
  (Visual Studio → `Audioslave.sln`) e com Ninja. `ctest` roda os testes.
- Saída de todos os executáveis em `build/bin` (`$<1:…>` evita subpasta por configuração).

## 13. Estratégia de Visual Studio

`cmake -S . -B build -G "Visual Studio 17 2022" -A x64` gera `build/Audioslave.sln` com os
alvos `Audioslave`, `AudioslaveService`, `AudioslaveSetup`, `audioslave_tests`,
`audioslave_core`, `audioslave_juce` (módulos JUCE agrupados em pastas). `VS_STARTUP_PROJECT`
= `Audioslave`. Também funciona "Open Folder" (CMake nativo do VS).

## 14. Estratégia de testes

- `juce::UnitTest`, executável `audioslave_tests` (console), registrado no CTest.
- Categorias: `Core`, `Config`, `Logging`, `Audio`, `IPC`, `Service`, `Tray`.
- Unitários com mocks (sem tocar dispositivos): todas as 44 verificações atuais portadas +
  novas: trava de pausa, `EndpointMemory`, remoção/recriação de endpoint, novas taxas,
  protocolo, `PipeServer`↔`ControlClient` reais (pipe com nome único), comandos inválidos,
  serviço indisponível, `ServiceHost` pause/resume/stop/reload com mocks, `TrayController`.
- Integração Windows **somente leitura** (`audioslave_tests --integration`): enumeração real,
  leitura do modo exclusivo, `IKsFormatSupport` no default, inventário JUCE WASAPI.
- Sistema (manual/scriptado, com elevação autorizada): instalar, serviço, pausa/retomada via
  SCM e IPC, bandeja, desinstalar.

## 15. Estratégia de installer

- `Audioslave-Setup.exe` = app JUCE (`requireAdministrator`), UI do assistente em componentes
  JUCE (mesmas opções: modo exclusivo obrigatório, padronização com taxa/profundidade, pasta,
  iniciar ao concluir). Pacote = zip (`Audioslave.exe`, `AudioslaveService.exe`) embutido como
  RCDATA e extraído com `juce::ZipFile`.
- Passos de sistema nativos (lógica reaproveitada do Audio Watchdog): parar versão em execução,
  gravar binários (renomeando se em uso), `Uninstall.exe`, config (preserva existente), ACLs,
  serviço + recuperação + DACL, origem do Event Log, Run (HKLM) para a bandeja, Menu Iniciar,
  Apps & Features, iniciar serviço, abrir bandeja não elevada.
- **Migração do Audio Watchdog**: se o serviço `AudioWatchdog` estiver instalado, o instalador
  (opção marcada por padrão; `/keeplegacy` para manter) executa o desinstalador silencioso dele
  e importa `C:\ProgramData\Audio Watchdog\config.ini` quando o Audioslave ainda não tem config.
  Dois watchdogs com alvos de formato diferentes brigariam pelo mesmo endpoint.
- Linha de comando: `/S`, `/format=48000:24`, `/noformat`, `/dir=`, `/notray`, `/keeplegacy`,
  `/uninstall [/S] [/removedata]`.
- `Audioslave-Portable.zip` (os dois executáveis + README) também é gerado.

## 16. Riscos

| Risco | Mitigação |
|---|---|
| `IPolicyConfig` / chaves `{B3F8FA53…}` não documentadas mudarem numa versão do Windows | Mesmo risco do Audio Watchdog; verificação por releitura; diagnóstico `validate`. |
| JUCE WASAPI mudar comportamento entre versões | Uso restrito a diagnóstico; submódulo fixado. |
| Pipe squatting (processo criando o pipe antes do serviço) | `FILE_FLAG_FIRST_PIPE_INSTANCE`; falha registrada; serviço sobe mesmo sem IPC. |
| Tamanho do executável com JUCE | Bandeja ~3–4 MB; aceitável. Serviço sem módulos de GUI referenciados. |
| Dois watchdogs no mesmo PC | Instalador remove o Audio Watchdog por padrão. |
| Teste real exige UAC/alteração do serviço na máquina de produção | Pedir autorização antes; testes de integração automáticos são somente leitura. |
| Licença | JUCE 9 é AGPLv3/comercial; o Audioslave é AGPL-3.0 → compatível. Uso comercial fechado exigiria licença JUCE. |

## 17. Compatibilidade

Windows 10 1607+ (mínimo da JUCE 9) e Windows 11, x64. Serviço sem usuário logado; bandeja por
sessão (inclusive RDP). Config e logs em novos caminhos (`C:\ProgramData\Audioslave`,
`<instalação>\logs`), com importação da config antiga do Audio Watchdog.

## 18. Plano de migração

1. Auditoria (feito) → este plano.
2. Esqueleto: `.gitignore`, submódulo JUCE, CMake, `audioslave_juce`, assets/ícones.
3. Plataforma Windows (RAII, erros, COM, caminhos/ACL, Event Log).
4. Config + Logger (JUCE).
5. Modelos + interfaces de áudio + implementações Windows.
6. Core: políticas, `EndpointMemory`, `WatchdogEngine` (juce::Thread).
7. IPC: protocolo, `PipeServer`, `ControlClient`.
8. Service: `ServiceHost`, `WindowsService`, `ServiceController`.
9. CLI + diagnóstico (+ sondas JUCE).
10. Testes (unit + integração).
11. Bandeja JUCE.
12. Instalador JUCE + desinstalador.
13. README, CHANGELOG, este documento (paridade).
14. Build Release, testes de sistema, release.

Commits por etapa (`Build:`, `Feat:`, `Test:`, `Docs:` …), push ao fim de cada etapa estável.

## 19. Critérios de paridade

| Funcionalidade | Audio Watchdog | Audioslave (meta) |
|---|---|---|
| Serviço (LocalSystem, auto, sem usuário logado) | OK | OK |
| Recuperação do serviço | OK | OK |
| Bandeja (tooltip, menu, ícone de pausa, Abrir) | OK | OK (JUCE) |
| Monitoramento por notificação + periódico | OK | OK |
| Novos / removidos / reconectados / recriados | OK | OK |
| Remoção do modo exclusivo + releitura | OK | OK |
| Validação comportamental | sonda WASAPI (diagnose) | sonda JUCE (diagnose/validate) |
| Formato: sample rate / bit depth, só se suportado | OK (44.1/48 kHz) | OK (44.1–192 kHz) |
| Configuração INI | OK | OK (+ importação) |
| Logs + rotação + Event Log | OK | OK |
| CLI | OK | OK (+ `rescan`, `validate`) |
| Pause / Resume (SCM, bandeja, CLI) | OK | OK (+ IPC push) |
| Shutdown limpo | OK | OK (corrige A1/A3) |
| Instalador / desinstalador / atualização | OK | OK (+ migração do Audio Watchdog) |
| Modo portátil | motor na bandeja | host portátil filho da bandeja |

A matriz final **medida** fica no fim deste documento após a etapa 20.

## 20. Critérios de conclusão

- Build Debug e Release (Ninja e Visual Studio) sem erros; testes passando.
- Todos os itens da §19 verificados na máquina real (ou justificativa documentada).
- Instalador instala serviço + bandeja + config + logs, serviço registrado corretamente;
  desinstalador remove tudo; atualização in-place funciona.
- Auditoria final: sem vazamentos conhecidos (leak detector da JUCE em Debug limpo), sem race
  conhecida, handles/COM com RAII, sem thread órfã, `.gitignore` correto, sem segredos.
