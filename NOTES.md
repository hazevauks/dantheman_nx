# dantheman_nx — notas do port

Port de **Dan the Man 1.2.1** (com.halfbrick.dantheman, versionCode 1210006,
armeabi-v7a) para Nintendo Switch sobre o runtime
[android32](https://github.com/aks796/android32) (submódulo em `runtime/`,
commit `50b352c`).

Estado: **compila e liga** (build no GitHub Actions, `.github/workflows/build.yml`,
nos containers do runtime). **Ainda não foi executado** em hardware nem emulador.

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

**A verificar no hardware:** se, com OpenSL recusado, o motor realmente cai em
`MAMAudioThread_AndroidJava` ou no `SoundManager` Java antigo (SoundPool), que
não está implementado. Se for o segundo, a alternativa é `RT_OPENSLES 1`.

## Entrada

- Teclas: `keyEvent(keyCode, down, flag, deviceId)` com os `KEYCODE_BUTTON_*` / `DPAD_*` do Android.
- Toque: `touchEvent(action, tempo, ponteiro, x/largura, y/altura, pressão, tamanho)` — coordenadas normalizadas 0..1.
- Controles: `onGameControllerAttach(deviceId, nome)` / `Detach(deviceId)`.
- Analógicos: `motionEvent(deviceId, eixo, x, y)` — **semântica dos eixos ainda não decodificada**; por ora o analógico esquerdo vira D-pad.

## Pendências

- [x] Build no GitHub Actions: libnx32 e mesa32 dos releases, `source/imports.c` gerado a cada build (224 imports, 0 faltando), NSP e NRO como artefato `dantheman_nx`
- [ ] `launcher/icon.jpg` é um provisório só com texto: trocar por um ícone definitivo (256×256)
- [ ] Primeiro teste no hardware: mandar `debug.log` e `crash.log`; a lista de métodos Java "unhandled" do log é a lista de tarefas de `dtm_java.c`
- [ ] Idioma: hoje fixo em `"en"`; ler o idioma do console
- [ ] Mapeamento final dos botões (A/B por rótulo ou por posição) e analógicos via `motionEvent`

## Ferramentas (`tools/`)

Sem binutils nem Python na máquina, dois scripts Perl fazem a análise:

- `elfinfo.pl <lib.so> [needed|exports|imports|jni|all]` — no lugar do `readelf`
- `dexinfo.pl <classes.dex> <regex de classe> [native | code [regex de método]]` — no lugar do `dexdump`
- `imports_needed.txt` — os símbolos que o jogo importa (gerado com `elfinfo.pl`; nomes de símbolos, não conteúdo do jogo)

## O que nunca vai para o repositório

O APK, a pasta extraída dele e `_refs/` (clones de outros ports só para
consulta) estão no `.gitignore`.
