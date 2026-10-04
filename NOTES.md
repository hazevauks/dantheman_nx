# dantheman_nx — notas do port

Port de **Dan the Man 1.2.1** (com.halfbrick.dantheman, versionCode 1210006,
armeabi-v7a) para Nintendo Switch sobre o runtime
[android32](https://github.com/aks796/android32) (submódulo em `runtime/`,
commit `50b352c`).

Estado: **roda no hardware** — entra no jogo, jogável até o primeiro checkpoint da
fase 1, sai sem crash. Build no GitHub Actions (`.github/workflows/build.yml`).

## O jogo

| | |
| --- | --- |
| Motor | Mortar, da própria Halfbrick (`libmortargame.so`, 13 MB, Thumb-2, gnustl embutido) |
| Gráficos | OpenGL ES 2 puro (73 funções `gl*`, sem EGL: o Java criava o contexto) |
| `DT_NEEDED` | libc, libm, liblog, libGLESv2, libandroid, libdl — só bibliotecas do sistema |
| Imports | 297; os 224 não-GL são libc/pthread/sockets comuns |
| Símbolos | **36 998 exportados** (C++ com nomes): dá para chamar/interceptar funções internas por nome |
| Outras libs | `libjs.so`, `libadcolony.so` (anúncios), `libcrashlytics.so` — não são carregadas |
| Assets | lidos pelo próprio motor de dentro do APK (`InitFileManager` recebe o caminho do APK) |

O runtime já cobre todos os imports menos 12: sete entram como `PASSTHROUGH`
em `tools/imports.cfg`, cinco têm shim em `source/dtm_libc.c`.

## Sequência de inicialização (do `GameManager` em classes2.dex)

1. `System.loadLibrary("mortargame")` → construtores (1006 entradas no init array) + `JNI_OnLoad`
2. `NativeGameLib.InitDeviceProperties()`
3. `InitFileManager(apk, filesDir + "/", cacheDir + "/", externalDir + "/", false)` — ordem confirmada pelos registradores
4. `InitOpenSLSoundManager(AssetManager)`; se devolver `false`, `InitJavaSoundManager()`
5. `SystemInit(largura, altura, idioma)`
6. `GameInit()`
7. por frame: `keyEvent(...)` das teclas enfileiradas, depois `step()`
   - `step() == false` → o jogo fecha
   - `gameRequestedQuit() == true` → diálogo "sair?" → `confirmQuitRequest(bool)`
   - `gameRequestedRestart() == true` → reinicia o app
8. `onPause` da Activity → `NativeGameLib.onPause()` + `saveOnExit()`

Assinaturas dos nativos: `perl tools/dexinfo.pl <apk>/classes2.dex '^Lcom/halfbrick/' native`.

## Áudio

O motor mixa sozinho (`Mortar::Audio::AudioMixer`) e tem duas saídas:
`MAMAudioThread_AndroidSLES` (OpenSL ES) e `MAMAudioThread_AndroidJava`, que
entrega PCM à classe Java `MortarAudioMixerOut` (um `AudioTrack` estéreo de
16 bits). O port usa a segunda: OpenSL fica recusado (padrão do runtime) e
`dtm_audio.c` responde `Create` / `GetNativeSampleRate` (48 kHz) / `Init` /
`WriteData` mandando os blocos para o audout.

Confirmado no hardware: `NativeGameLib.SupportsOpenSL()` responde `false`, o motor
cria `MAMAudioThread_AndroidJava` e escreve 2004 frames estéreo por vez.

**Atenção:** `Init(rate)` devolve a própria taxa recebida, não um tamanho de buffer.
O motor usa esse valor como taxa de saída e reamostra a mixagem (44,1 kHz interna)
para ela; devolver outro número acelera e distorce o som.

## Entrada

- Teclas: `keyEvent(keyCode, down, flag, deviceId)` com os `KEYCODE_BUTTON_*` / `DPAD_*` do Android.
- Toque: `touchEvent(action, tempo, ponteiro, x/largura, y/altura, pressão, tamanho)` — coordenadas normalizadas 0..1.
- Controles: `onGameControllerAttach(deviceId, nome)` / `Detach(deviceId)`.
- Analógicos: `motionEvent(deviceId, eixo, x, y)` — **semântica dos eixos ainda não decodificada**; por ora o analógico esquerdo vira D-pad.

## Pendências

- [x] Build no GitHub Actions: libnx32 e mesa32 dos releases, `source/imports.c` gerado a cada build (224 imports, 0 faltando), NSP e NRO como artefato `dantheman_nx`
- [ ] Primeiro teste no hardware: mandar `debug.log` e `crash.log`; a lista de métodos Java "unhandled" do log é a lista de tarefas de `dtm_java.c`
- [ ] Idioma: hoje fixo em `"en"`; ler o idioma do console
- [x] Botões por posição: B pula/confirma, Y bate (`swap_a_b` no config.ini troca A e B)
- [ ] Analógicos via `motionEvent`
- [ ] Conferir se o save persiste entre execuções (`KeyStore` agora grava em `data/keystore.txt`)

## Ferramentas (`tools/`)

Sem binutils nem Python na máquina, dois scripts Perl fazem a análise:

- `elfinfo.pl <lib.so> [needed|exports|imports|jni|all]` — no lugar do `readelf`
- `dexinfo.pl <classes.dex> <regex de classe> [native | code [regex de método]]` — no lugar do `dexdump`
- `imports_needed.txt` — os símbolos que o jogo importa (gerado com `elfinfo.pl`; nomes de símbolos, não conteúdo do jogo)

## O que nunca vai para o repositório

O APK, a pasta extraída dele e `_refs/` (clones de outros ports só para
consulta) estão no `.gitignore`.

## Achados dos testes no hardware

- **Firebase**: o SDK aborta se não carrega suas classes Java. O jogo só o usa
  pela camada `FirebaseNS`, cujas 18 funções são substituídas por stubs
  (`dtm_firebase.c`); os valores de remote config são os padrões que o jogo
  passa a `FirebaseNS::Init` (14 pares chave/valor, layout confirmado no log).
- **Threads do motor**: cada pthread do motor chama
  `NativeGameLib.native_threadEntry(int)` via JNI; o handler repassa ao nativo
  registrado em `JNI_OnLoad`. Sem isso nenhuma thread do motor trabalha.
- **`HBSupport`**: consultas de dispositivo respondidas em `dtm_java.c`
  (IDs constantes, Android 23, 240 dpi, multitoque, não é TV nem tablet).
- **Caminhos `data/app/…/base.apk/<arquivo>`** no log: o motor procura cada
  arquivo também num "mount" do APK com caminho relativo; essas tentativas
  falham e ele segue para o caminho certo. Só ruído.
- `tools/thumbcalls.pl` lista o que uma função Thumb do motor chama e as
  constantes em volta — foi como o problema do áudio foi achado.

## Revisão antes do lançamento

- **HOME / repouso**: o port chamava `NativeGameLib.onResume`, que no Android
  serve para contexto GL perdido e descarrega e recarrega todas as texturas
  (`DisplayManager_Android::UnloadAllResources` / `ReloadAllResources`), e
  ainda passava um array nulo. Trocado pelo caminho leve do Android para perda
  de foco sem perda de contexto: `onFocusLost` + `saveOnExit` ao sair,
  `onFocusRetrieved` ao voltar. **Ainda não testado no hardware.**
- **Idioma**: lido do console (`set:sys`) e passado a `SystemInit` e ao
  `HBSupport`; `[game] language` no config.ini força outro. O jogo tem en, es,
  es-419, de, fr, it, ja, pt, ru, tr, zh (os dois). **Ainda não testado.**
- **Carregamentos**: `RT_BOOST_WATCH_THREAD 1` acelera a CPU durante frames
  longos (as trocas de fase levavam 2-3 s num frame só).
- **Volume**: `GetMusicStreamVolume` / `MaxVolume` respondem 15 de 15.

### Em aberto

- **Portões de fase ("gate system")**: o jogo tem um sistema que libera fases
  por anúncios assistidos ou por tempo de espera (`gate_system_mins_per_ad`,
  `gate_system_max_ads_to_unlock`, `GameScreenStoryMap::InitGateSystemCountdownAssets`)
  e uma compra "Premium" que os remove. Sem anúncios no Switch, é preciso jogar
  além da primeira fase para saber se algum portão aparece e se a espera
  funciona offline. Não foi mexido.
- Analógicos via `motionEvent` (hoje o esquerdo vira D-pad).
- 2 jogadores, modo dock (1080p) e toque não foram exercitados nos testes.
- O ícone do launcher é arte do jogo fornecida pelo autor do port.
