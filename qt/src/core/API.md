# API do núcleo (`openframe_core`)

Índice para quem usa o núcleo (app QML, CI). Os detalhes ficam nos cabeçalhos.
Tudo no namespace `of`; tempo em `Micros` (microssegundos, `qint64`).

## Etapa 1: modelo e edição

| Cabeçalho | O que tem |
|---|---|
| `Timing.h` | `Micros`, `Time::second/seconds()/toSeconds()/round()/format()`, `TimeRange` |
| `Enums.h` | `enumKey(e)` / `enumFromKey<E>(s)` (o `rawValue` do JSON), `allCases<E>()` para menus |
| `Effects.h` | `RGBA`, `Transform`, `Adjust`, `FilterPreset`/`FilterSetting`, `VideoEffect`, `SegmentAnimation`, `Transition`, `TextStyle`, `TextContent`, `displayName(...)` |
| `Project.h` | `Material`, `Segment` (`targetDuration/targetEnd/targetRange`, `Segment::makeText`), `Track`, `AspectRatio` (`label`, `ratio`), `CanvasBackground` (`black()/color()/blur()`), `Canvas`, `Project` (`duration`, `material`, `mainTrackIndex`, `locate`, `segment`, `aspectValue`, `canvasSize`) |
| `ProjectStore.h` | `encode/decode/save/load`, `toJson/fromJson`, `uuidString`, `fileExtension`; JSON igual ao do Swift, com os campos de motion e legendas (ver abaixo); tipo de animação desconhecido (versão futura) abre como `fade` e loop desconhecido é descartado |
| `TimelineEditor.h` | as operações do Swift sobre `Project&` (`addMedia`, `addOverlay`, `insertMedia`, `addText`, `split`, `remove`, `duplicate`, `move`, `reorderMain`, `resolveOverlap`, `canMove`, `moveToTrack`, `trimStart`, `trimEnd`, `setSpeed`, `update`, `snapPoints`, `snap`, `snapMove`) |
| `Subtitles.h` | `SubtitleCue`, `SRT::parse/serialize/importCues/cues` |
| `ExportSettings.h` | `ResolutionPreset`, `FrameRate` (`frameDuration` racional, `fpsValue`), `VideoCodec`, `ContainerFormat`, `BitrateMode` (todos com `label`), `ExportSettings` (`outputSize`, `videoBitrate`, `estimatedBytes`, `validationError`, `keyframeDistance`), `formattedFileSize` |
| `ProjectLibrary.h` | `ProjectEntry`, `ProjectLibrary` (`list`, `entry`, `create`, `save`, `load`, `rename`, `duplicate`, `remove`, `importProject`, `writeThumbnail`, `nextName`, `defaultRoot`) |
| `FontCatalog.h` | `FontCatalog::families/faces` (precisa de `QGuiApplication`) |

## Etapa 2: mídia (FFmpeg)

| Cabeçalho | O que tem |
|---|---|
| `MediaProbe.h` | `MediaProbe::isSupported/isImageExtension/isAudioExtension`, `MediaProbe::probe(path, QString*) -> optional<Material>` (erros "Formato não suportado: x" e "Não foi possível ler: x"); `Images::load(path, maxSide=0, QString*)`: foto orientada (EXIF), sRGB, pré-multiplicada |
| `VideoDecoder.h` | `VideoDecoder::open(path, QString*) -> unique_ptr`; `frameAt(t, maxSize={})` com busca exata e leitura sequencial, `displaySize`, `duration`, `lastFrameTime`; `DecoderCache(capacity)` com `frame(segmentId, path, t, maxSize)` e `clear()` (thread-safe) |
| `Thumbnails.h` | `Thumbnails::frames(material, count=10, maxSide=240)` |
| `Waveform.h` | `Waveform::peaks(path, perSecond=50, QString*)`: picos normalizados (0…1) |
| `MediaWriter.h` | `MediaWriter::create(path, optional<Video>, optional<Audio>, QString*)`; `writeVideo(QImage, index)`, `writeAudio(interleaved, frames)`, `finish(QString*)`, `abort()`. H.264, HEVC (hvc1), ProRes 422, AAC; contêiner pela extensão (MP4, MOV, M4A) |

Cores: matriz e intervalo do arquivo; sem marca, BT.709 em HD e BT.601 em SD.
Rotação da trilha e EXIF já aplicados nos quadros. Vídeo HDR (BT.2100 HLG ou PQ,
como os do iPhone) sai convertido para SDR como o macOS mostra (ver
`ToneMap.h`); cinzas a até 3 níveis do macOS, cores comuns a até 12.

## Etapa 3: render na CPU e texto

| Cabeçalho | O que tem |
|---|---|
| `Renderer.h` | `Renderer::shared().render(project, t, size) -> QImage` (thread-safe; o tamanho define a escala, efeitos usam altura/1080), `clearCaches()`; `EffectPreview::filtered(image, preset)` para as miniaturas da aba de filtros |
| `TextRenderer.h` | `TextRenderer::shared().image(content, canvas, reveal=1)`, `TextRenderer::cased(text, textCase)` (precisa de `QGuiApplication`) |

Para o app:
- `render` não é estático: use `Renderer::shared()` (ou uma instância própria,
  que tem seus próprios caches de decodificadores).
- A saída é sRGB 8 bits opaca (`Format_ARGB32_Premultiplied`).
- Miniatura do projeto: `render` em 1/3 da duração (até 2 s), lado menor 360,
  gravada pelo app com `ProjectLibrary::writeThumbnail`.

Internos (não usar no app): `LinearImage`, `ImageEffects`, `Motion`
(`Animations`, `Transitions`), `FilterModels`.

Diferenças em relação ao Swift:
- Tudo é filtrado e composto em luz linear, como o Core Image; a saída usa a
  curva sRGB (o Swift grava com a 1,961 do macOS).
- Os filtros fotográficos (Mono, Noir, Tonal, Cromo, Desbotado, Instantânea,
  Processo, Transferência, Sépia) são aproximações ajustadas às cores medidas
  no Swift (erro médio de 0,2 a 8 níveis por canal; ver `FilterModels.h`).

## Etapa 4: áudio e reprodução

| Cabeçalho | O que tem |
|---|---|
| `Player.h` | `Player` (QObject, thread principal): `setProject(p)` a cada edição, `setPreviewShortSide(720)`, `play()`, `pause()`, `seek(t)`, `time()`, `duration()`, `isPlaying()`, `setAudioEnabled(bool)`; sinais `frameReady(QImage)`, `timeChanged(Micros)` (~30 Hz tocando, e em `seek`/`pause`), `playingChanged(bool)`, `finished()` |
| `AudioMix.h` | `AudioMix::clips(project)` (o que soa, com as regras do Swift), `AudioMix::gain`, `AudioMixer` (48 kHz estéreo float, `seek`/`read`); base do Player e da exportação |

Para o app:
- `setProject` pode ser chamado a cada mudança: pausado, redesenha o quadro;
  o som só é refeito quando trechos, volume, fades ou velocidade mudam.
- `seek` é perseguido: no scrub, pode chamar à vontade; só o mais recente é
  desenhado depois do quadro em andamento.
- Tocando, a resolução cai sozinha (até 1/3 de `previewShortSide`) se o render
  não acompanhar; pausado, o quadro sai na resolução cheia.
- Ao chegar ao fim: `playingChanged(false)`, `timeChanged(duração)`, último
  quadro e `finished()`. `play()` no fim recomeça do zero.
- O som sai pelo `QAudioSink` (Qt Multimedia) numa thread própria. Sem
  dispositivo de som, toca mudo. Só usa `QAudioSink`/`QMediaDevices`: o
  plugin de mídia FFmpeg do Qt não é necessário.
- O som fica preso ao relógio do vídeo (`ClockedMixer`, em `AudioMix.h`): a
  primeira leitura do dispositivo pula o tempo que ele levou para partir (no
  Windows, perto de 1 s numa VM; no Mac, ~0,1 s), então o som sai no instante
  que o vídeo mostra. O dispositivo e o formato são escolhidos uma vez (já no
  `setProject` com som) e refeitos quando a saída padrão muda.
  `OPENFRAME_AUDIO_LOG=1` mostra quanto foi pulado em cada play.

## Etapa 5: exportação e miniatura

| Cabeçalho | O que tem |
|---|---|
| `Exporter.h` | `Exporter` (QObject): `start(project, settings, path)`, `cancel()`, `isRunning()`; sinais `progress(double)` (0…1, crescente, 1 no fim) e `finished(bool ok, QString error)` |
| `Renderer.h` | `ProjectThumbnail::image(project)` e `ProjectThumbnail::jpeg(project)` (quadro em 1/3 da duração, até 2 s, lado menor 360; JPEG qualidade 80) |

Para o app:
- `start` volta na hora; a exportação roda numa thread própria e os sinais
  chegam na thread do `Exporter`.
- Configuração inválida (`ExportSettings::validationError`) ou M4A sem som
  terminam com `finished(false, mensagem)`. Cancelado: `finished(false, "")`,
  e o arquivo parcial é apagado. Destruir o `Exporter` também cancela.
- Formato do arquivo: H.264 (x264 "medium"), HEVC (x265 "fast", tag hvc1) ou
  ProRes 422 (só MOV), com AAC na taxa escolhida (44,1 ou 48 kHz) e estéreo;
  bitrate, fps e quadros-chave do `ExportSettings`; marcas BT.709; índice no
  começo do arquivo (faststart). O som vai até o fim do vídeo (silêncio depois
  do último trecho).
- Miniatura: `library.writeThumbnail(ProjectThumbnail::jpeg(project), id)`
  depois de salvar (num `QtConcurrent::run`, para não travar a interface).

Convenções:
- Enums são `enum class` em PascalCase (`AspectRatio::R9x16`, `TrackKind::Text`).
- O `throws` do Swift virou `std::optional`/`bool` com `QString *error` opcional.
- `TimelineEditor.delete` do Swift chama-se `remove` (palavra reservada em C++).
- Projeto novo (`Project{}`) já vem com a faixa principal, como no Swift.

## Motion e legendas, etapa 1: modelo, JSON, geometria, edição e montagem das legendas

Porte do que entrou no Mac depois da v0.5.0, com os mesmos nomes e números do núcleo Swift. O render novo
(animações por unidade, loops, revelações, ênfase desenhada por palavra) vem na etapa 2; a transcrição real, na 3.

| Cabeçalho | O que tem |
|---|---|
| `Color.h` | `RGBA` (saiu do `Effects.h`, que continua incluindo) |
| `Captions.h` | `CaptionWord{text,start,end}` (relativo ao segmento), `HighlightMode` (current/progressive), `WordReveal` (none/rise/dim/fill/type, com `displayName`), `WordHighlight{color,scale,backgroundEnabled,backgroundColor,mode,reveal,dimOpacity}`, `WordEmphasis{words,fontName,fontFace,scale,color?}`, `TranscribedWord{text,start,end,probability}` (tempos da timeline) |
| `Effects.h` | `AnimationKind` com os 17 tipos (`isTextOnly`), `LoopKind`/`LoopAnimation{kind,period,intensity}`, `TextStyle::balanceLines/fitLines` e `maxWidthMin/maxWidthMax` (0,1…3), `TextContent::words/highlight/emphasis` (opcionais), `TextContent::wordsOf(text)` (separa por espaço, inclusive U+00A0 e `\n`) e `activeHighlight()` (vazio se a contagem não bate). `TextContent(text, style = {})` tem construtor |
| `Project.h` | `Segment::animationLoop`, `Track::isAutoCaptions` |
| `ProjectStore.h` | grava e lê tudo isso como o Swift: `animationLoop`, `words`, `highlight`, `emphasis` e `isAutoCaptions` só quando existem (`isAutoCaptions` só quando verdadeiro), `balanceLines`/`fitLines` sempre. No destaque e na ênfase, campo ausente fica com o padrão e `mode`/`reveal` desconhecido vira o padrão |
| `CanvasLayout.h` | `ElementBox{id,isText,center:QPointF,size:QSizeF,scale,rotation}`; `CanvasLayout::boxes(project, t)` (vídeo/foto das faixas visíveis, depois os textos), `fitSize`, `hitTest(point, boxes, aspect) -> optional<QUuid>`, `corners(box, aspect) -> QList<QPointF>` (sup. esq., sup. dir., inf. dir., inf. esq.), `bounds(box, aspect) -> QRectF` (y para cima: `top()` é o menor y); `SnapGuide{axis,position,kind}`; `Snapping::snapMove(box, proposed, others, aspect, threshold:QSizeF) -> MoveResult{center, guides}`, `Snapping::snapRotation(degrees, threshold = 4) -> RotationResult{degrees, snapped}`, `Snapping::safeMargin` (0,9). Coordenadas normalizadas (-1…1, y para cima) |
| `TimelineEditor.h` | `setPlacement(p, id, center:QPointF, scale, rotation)` (estilo no texto, `transform` no visual; escala mínima 0,01), `setTextWidth(p, id, maxWidth)` (limitada a 0,1…3, ignora NaN/infinito; numa faixa automática vale para todos os textos dela). `freeTrack` pula a faixa automática; `split` e `trimStart` mantêm as palavras no mesmo instante da timeline |
| `TextRenderer.h` | `boxSize(content, canvas) -> optional<QSizeF>` (caixa do texto + fundo, com palavra-chave, `fitLines` e `balanceLines`; bate com o Swift a até 2 px), `lineWidth(text, style, fontPx)`, `lineTexts(content, canvas)`. A imagem estática já usa o mesmo layout (palavra-chave na fonte e na cor dela, linhas ajustadas e equilibradas) |
| `CaptionPresets.h` | `CaptionLayout` (phrases/highlight/punch/stacked), `CaptionPresetGroup`, `CaptionPreset` (os 10 visuais) com `displayName`, `detail`, `group`, `layout`, `usesAccent`; `presetFor(layout)` |
| `CaptionBuilder.h` | `CaptionCue{text,start,end,words}`; `CaptionBuilder::cues(words, preset, aspect = 16/9)` e `cues(words, layout, aspect)`; `apply(cues, preset, accent?, project, replacing, range?) -> QUuid` e `apply(cues, layout, style?, ...)`; `defaultStyle(preset)` e `defaultStyle(preset, aspect)`; `keywords(cue)`, `highlight`, `emphasis`, `animate`, `normalized`, `limits`, `Measure`; constantes `glue` (U+00A0), `minDuration`, `closeGap`, `pause`, `forcedBreak`. Mede com o `TextRenderer`: precisa de `QGuiApplication` |
| `WhisperModels.h` | `WhisperModel` (base/small/turbo) com `displayName` ("Rápido", "Equilibrado", "Máxima precisão"), `fileName`, `byteSize`, `downloadUrl`, `sha256`; `recommendedModel()` = small (só CPU nesta versão). `ModelStore` (QObject): `shared()`, `defaultDirectory()` (`AppLocalDataLocation/Models` ou `OPENFRAME_MODELS_DIR`), `path`, `isInstalled`, `remove`, `download`/`cancel`/`isDownloading` com os sinais `downloadProgress(model, fraction)` e `downloadFinished(model, ok, error)` |
| `Transcriber.h` | `TranscriptionProgress{phase,fraction,overall}` (`make`; a mistura pesa 10%), `TranscriptionSource` (`timeline()`, `of(id)`), `TranscriptionError` e `message(...)`; `Transcriber` (QObject, como o `Exporter`): `start(project, source, model, language = "")`, `cancel()`, `isRunning()`, sinais `progress(TranscriptionProgress)` e `finished(ok, words, error)` (cancelado = ok falso e erro vazio); estáticos `sourceRange`, `mixProject`, `languages()`, `fakeWords(range)` |

Provisório nesta etapa (o render ficou pronto na etapa 2 e a transcrição na 3, abaixo):
- `ModelStore::download` terminava com o erro "O download dos modelos ainda não está disponível nesta versão.".
- `Transcriber`: com `OPENFRAME_FAKE_TRANSCRIPTION=1`, palavras falsas pelo trecho (~0,8 s de progresso falso);
  sem ela, "modelo não instalado" ou "motor indisponível".
- Diferença do Swift que fica: sem o etiquetador gramatical do macOS, a palavra-chave é escolhida pelo tamanho, pelo
  fim de frase, pelas listas de palavras funcionais e fracas, e os advérbios em "-mente" perdem peso; no empate, a
  mais adiante. Em textos comuns, escolhe o mesmo substantivo longo que o Mac; pode divergir quando um verbo e um
  substantivo têm o mesmo tamanho.
- Fontes: o mapeamento para fontes distribuíveis entrou na etapa 2 (abaixo).

## Motion e legendas, etapa 2: render do motion e das legendas

As 17 animações, os 4 loops (em texto, foto e vídeo), o texto por unidade, o destaque da palavra, as revelações, a
ênfase e o `fitLines` agora aparecem no `Renderer` (preview, exportação e miniaturas), com as regras e os números
do Swift. Nada muda na chamada do app: `Renderer::shared().render(project, t, size)` desenha tudo.

| Cabeçalho | O que tem |
|---|---|
| `AnimationPreview.h` | `AnimationPreview::frames(subject, part, size, count) -> QList<QImage>`: miniaturas animadas da aba Animação e dos cartões da janela das legendas, pelo mesmo `Renderer` (prévia = resultado). `Subject` = `TextContent` ou `QImage` (foto ou quadro do vídeo); `Part::entry(kind)`, `Part::exit(kind)`, `Part::loopOf(loopKind)`. Entrada: 600 ms de animação e 600 ms parado; saída: parado e depois a saída; loop: dois períodos na intensidade máxima. Texto centralizado e com a fonte ajustada para caber (nítido); imagem encaixada. Fundo transparente (`Format_ARGB32_Premultiplied`). Vazio com texto vazio, imagem nula ou `count` ≤ 0. Precisa de `QGuiApplication`; pode rodar fora da thread da interface |
| `Renderer.h` | `RenderOptions{stills, transparent}` e `render(project, t, size, options)`: fotos em memória por material e fundo transparente (é o que o `AnimationPreview` usa; o app não precisa) |
| `FontCatalog.h` | `registerBundled()` (chamada sozinha pelo `TextRenderer` e por `families()`) e `resolve(family, face) -> ResolvedFont{family, face}`: a família pedida, se existir; senão a equivalente distribuída (abaixo). `families()` passa a listar também as fontes do núcleo. `OPENFRAME_BUNDLED_FONTS_ONLY=1` (testes): só as distribuídas contam como existentes, e o Mac resolve como o Windows e o Linux; no Mac, o ctest roda o CaptionTests, o CaptionRenderTests e o TextLayoutTests de novo assim (`*BundledFonts`) |
| `TextRenderer.h` | `imageSize(content, canvas)`, `rasterScale(content, canvas, peak)`: o texto é rasterizado na escala efetiva (estilo × pico da animação, passos de 1/8, mínimo 1, lado de até 8192 px) e só reduzido depois |

Fontes embutidas no núcleo (recurso `:/openframe/fonts`, 2,6 MB; todas SIL Open Font License 1.1, com o texto da
licença em `qt/src/core/fonts/OFL-*.txt`). **Para o `TERCEIROS.txt` (05):**
- Inter 4.001 (Regular, Light, Medium, Bold), © 2016 The Inter Project Authors, https://github.com/rsms/inter;
- JetBrains Mono 2.304 (Regular, Bold), © 2020 The JetBrains Mono Project Authors,
  https://github.com/JetBrains/JetBrainsMono;
- Playfair Display Italic (variável), © 2017 The Playfair Display Project Authors, nome reservado "Playfair Display",
  https://github.com/clauseggers/Playfair-Display;
- Barlow Condensed (Bold, Black), © 2017 The Barlow Project Authors, https://github.com/jpt/barlow.

Mapeamento (só quando a família pedida não existe, ou seja, num projeto vindo do Mac aberto no Windows/Linux):
Helvetica Neue, Avenir Next, Futura, Gill Sans e SF Pro → Inter, com o estilo "Condensed Black/Bold" → Barlow
Condensed Black/Bold; Menlo, Monaco, SF Mono, Courier, Courier New e American Typewriter → JetBrains Mono; Didot,
Bodoni 72 e Baskerville → Playfair Display; qualquer outra ausente → Inter. No Mac, as fontes do sistema existem e
são usadas.

Comparação com o Swift (`qt/tests/tools/gerar-motion.sh` → `qt/tests/fixtures/motion.json`, teste
`MotionReferenceTests`): 66 casos, 301 quadros (cada animação na entrada e na saída, os loops em texto e foto, as
revelações, o destaque, a ênfase, o `fitLines` e as 10 predefinições, com as fontes OFL dos dois lados). Medido no
Mac, num quadro de 480×270: caixa da tinta ≤2 px, centroide ≤1,3 px, brilho médio ≤0,42 de 255, cor média por
região (grade 8×4) ≤7,6 níveis. O Core Text engrossa um pouco o traço ao suavizar: o Qt tem ~6% menos pixels acima
do limiar nas letras grandes (até 20% nas pequenas ou quase sumindo), sem diferença visível.

Desempenho (mediana por quadro, build Release, Apple M5 de 10 núcleos, só o texto sobre o fundo preto):

| Caso | 1080p | 720p |
|---|---|---|
| 40 letras por unidade + reflexo (cascata, elástico, decodificar, máscara) | 11–13 ms | 5–8 ms |
| Legendas (Surgir, Empilhado, Preenchimento, Digitação, Editorial) | 4–9 ms | 2–6 ms |
| Arraste da escala de 1× a 3× (estático / cascata) | 6 / 13 ms | — |

A primeira montagem dos sprites de um texto custa 10–110 ms (fica em cache). Sem otimização (build Debug), tudo
fica ~6× mais lento.

## Motion e legendas, etapa 3: transcrição com o whisper.cpp na CPU

O `Transcriber` e o `ModelStore` deixaram de ser provisórios. A API do app não mudou (é a da etapa 1):
`ModelStore::shared().download/cancel/remove/isInstalled` e `Transcriber::start/cancel` com os mesmos sinais.

| Cabeçalho | O que mudou |
|---|---|
| `Transcriber.h` | `start` mistura o áudio como a exportação ouve (`AudioMixdown`: o `AudioMixer` em 48 kHz, somado para mono e levado a 16 kHz), transcreve com o whisper e devolve as palavras com os tempos da timeline. Erros: "modelo não instalado", "sem áudio" (silêncio ou nenhum som no trecho), "motor indisponível" (nenhuma variante de CPU encontrada), "A transcrição falhou: …". `cancel()` interrompe o whisper de verdade (~0,3 s medido). Novo: `Transcriber::run(...)`, a mesma coisa síncrona (testes e medições). `OPENFRAME_FAKE_TRANSCRIPTION=1` continua valendo |
| `WhisperModels.h` | `ModelStore::download` baixa por `QNetworkAccessManager` para `<arquivo>.partial`, retoma com `Range` (se o servidor responder 200, recomeça), confere o tamanho e o sha256 (fora da thread da interface) e só então move para o lugar. Cancelar guarda o parcial (o próximo download continua); `remove` também cancela e solta o modelo da memória. `downloadProgress` no máximo a cada ~100 ms; o aviso que cair antes é adiado, não descartado (o último valor antes de uma pausa da rede sempre chega). Mensagens: "O servidor do modelo respondeu com o erro N.", "O download veio incompleto (X de Y bytes). Tente de novo.", "O arquivo baixado está corrompido (o sha256 não confere). Tente de novo.", "Não foi possível baixar o modelo: …". Novos: `partialPath(model)`, `sha256Of(file)` e `download(model, url, size, sha256)` (testes) |

Internos (não usar no app): `WhisperEngine.h` (contexto do modelo em cache, liberado depois de 3 min parado;
`isAvailable`, `loadBackends`, `backendDescription`; `Options::threads` = 0 escolhe as threads sozinho com
`busyCores` e `threadsFor`, veja "Threads e CPU ocupada"), `WordAligner.h` (ajuste das fronteiras pela energia do
áudio), `AudioMixdown.h`.

Variáveis de ambiente:
- `OPENFRAME_MODELS_DIR`: pasta dos modelos (padrão: `AppLocalDataLocation/Models`);
- `OPENFRAME_GGML_DIR`: única pasta onde procurar as variantes do backend CPU (padrão: veja abaixo);
- `OPENFRAME_WHISPER_LOG=1`: mostra os logs do whisper/ggml no stderr;
- nos testes: `OPENFRAME_TEST_MODELS_DIR` (única pasta do modelo), `OPENFRAME_REQUIRE_WHISPER_MODEL=1` (sem o
  modelo, falha em vez de pular; a CI usa) e `OPENFRAME_BENCH=1` (mede 1 min de fala com cada modelo; com
  `OPENFRAME_TEST_MODEL=base` só um; `OPENFRAME_BENCH_THREADS=8,4` mede cada número de threads, e 0 é a escolha
  automática do app). As esperas dos testes do whisper não têm prazo próprio: o limite é o TIMEOUT do ctest (900 s),
  e o vigia do Qt Test (`QTEST_FUNCTION_TIMEOUT`) vai para o mesmo valor.

### Build

- `qt/cmake/Whisper.cmake`: whisper.cpp v1.9.5 (`d1be6fde11ac6e0407606b4e42fe72d34add8037`, o mesmo do Mac)
  por FetchContent, do tarball do commit com sha256 fixo (o configure baixa ~10 MB; sem rede, aponte
  `FETCHCONTENT_SOURCE_DIR_WHISPER` para uma cópia do mesmo commit). Só o backend CPU: Metal, CUDA, Vulkan, BLAS
  e OpenMP desligados; `GGML_NATIVE=OFF`.
- `OPENFRAME_WHISPER_VARIANTS=ON` (padrão): `GGML_BACKEND_DL` + `GGML_CPU_ALL_VARIANTS`, com o whisper e o ggml
  como bibliotecas compartilhadas. Desligado: tudo estático, só a variante genérica (mais lenta).
  `OPENFRAME_WHISPER=OFF` compila sem a transcrição ("motor indisponível").
- As bibliotecas saem em `OPENFRAME_WHISPER_LIB_DIR` (padrão `<build>/ggml`). O Qt passa a precisar do
  componente **Network** (`find_package` no `qt/CMakeLists.txt`).
- **Para o app no Windows (05):** chame `openframe_deploy_whisper(<alvo>)` (em `qt/cmake/Whisper.cmake`) no
  CMake do app; ele copia as DLLs do whisper/ggml para a pasta do executável depois do build (sem elas, o
  executável nem abre). Os testes já recebem a pasta no `PATH`.

### Arquivos que o pacote leva (05)

O núcleo carrega a variante na primeira transcrição (nunca ao abrir o app) e procura nesta ordem: a pasta real do
executável (`WhisperEngine::executableDirectory`: links resolvidos; no Linux, o `/proc/self/exe`, porque o AppImage
abre pelo `AppRun`, um link na raiz do AppDir, e o `applicationDirPath()` do Qt dava essa raiz),
`<pasta do executável>/../lib/openframe`, `$APPDIR/usr/lib/openframe` (se `APPDIR` existir) e, só para um executável
dentro da árvore do build
(`OPENFRAME_BUILD_ROOT`, nunca num pacote), a pasta do build (`WhisperEngine::backendSearchPath`). Sem nenhuma variante,
a transcrição termina com "motor indisponível", sem travar. Com `OPENFRAME_WHISPER_LOG=1`, o núcleo mostra no stderr
(`[openframe] …`) cada pasta tentada e, para cada variante, se ela abre (com a nota) ou o erro do sistema
(`dlerror`/`LoadLibrary`): num build com otimização, o ggml não mostra por que uma variante falhou.

**Windows (MSVC):** tudo ao lado do `OpenFrame.exe` (o windeployqt não conhece estes arquivos):
- `whisper.dll`, `ggml.dll`, `ggml-base.dll`;
- 9 variantes: `ggml-cpu-x64.dll`, `ggml-cpu-sse42.dll`, `ggml-cpu-sandybridge.dll`, `ggml-cpu-haswell.dll`,
  `ggml-cpu-skylakex.dll`, `ggml-cpu-cannonlake.dll`, `ggml-cpu-cascadelake.dll`, `ggml-cpu-icelake.dll`,
  `ggml-cpu-alderlake.dll` (o MSVC não monta ivybridge, piledriver, cooperlake, zen4 nem sapphirerapids).

**Linux (AppImage):**
- em `usr/lib` (o linuxdeploy já as leva, porque o executável depende delas): `libwhisper.so.1`,
  `libggml.so.0`, `libggml-base.so.0`;
- em `usr/lib/openframe/` (o linuxdeploy **não** as conhece: são carregadas com `dlopen`): as 14 variantes
  `libggml-cpu-{x64,sse42,sandybridge,ivybridge,piledriver,haswell,skylakex,cannonlake,cascadelake,icelake,cooperlake,zen4,alderlake,sapphirerapids}.so`.

É só copiar o conteúdo de `OPENFRAME_WHISPER_LIB_DIR` com essas regras. O `cmake --install` instala só o app: o
whisper/ggml entra fora do ALL (`EXCLUDE_FROM_ALL`), sem as regras de instalação dele (cabeçalhos, pkg-config, CMake,
o Parakeet). No Linux, o linuxdeploy traz as 3 de `usr/lib` como dependências do executável (com
`OPENFRAME_WHISPER_LIB_DIR` no `LD_LIBRARY_PATH`), e as variantes são copiadas. As variantes compilam pelo alvo
`openframe_ggml_cpu` (no ALL), e quem liga com o `openframe::whisper` espera por elas. Tamanho: no Mac, cada variante tem
~0,9 MB, o ggml-base 0,75 MB e o whisper 0,4 MB; no x86, falta medir (estimo 10–15 MB no total).

**Rede:** o download usa HTTPS (huggingface.co): o pacote precisa do Qt Network e do plugin de TLS
(`plugins/tls`: `qschannelbackend` no Windows; `qopensslbackend` no Linux, que usa a OpenSSL do sistema). Vale
testar um download de verdade no pacote. É o único acesso à rede do app, só quando o usuário pede.

**Proxy:** o download usa o proxy do sistema, mas a consulta roda numa thread de trabalho, antes do pedido. No Windows,
com a detecção automática (WPAD) ligada, o Qt chama o `WinHttpGetProxyForUrl` antes de ver que o endereço é local, e
isso pode travar a thread do pedido por dezenas de segundos para quem tem a detecção ligada. (Na CI do Windows não
era isso: o runner não tem proxy nem WPAD, e os 34,7 s do WhisperUnitTests eram o progresso que não passava do
primeiro aviso, corrigido no `fb264c2`.) O resultado da consulta vale também para os redirecionamentos (o CDN do
Hugging Face). Endereço local (loopback, `localhost`, `file://`) vai direto, sem consulta. Cancelar durante a consulta
termina na hora.

**Testes no Windows:** sem um console (o ctest da CI), o Qt Test escreve no `OutputDebugString`, e o log do ctest fica
vazio. Por isso todos os testes recebem `QT_FORCE_STDERR_LOGGING=1` e escrevem no stdout.

### Medições (CPU, 65 s de fala do `say`, build Release)

Apple M5, 8 threads, variante apple_m4 (sem o Accelerate, que só o Mac tem):

| Modelo | 1ª vez (com o carregamento) | 2ª vez | WER |
|---|---|---|---|
| base | 4,7 s | 4,4 s | 0 |
| small (recomendado) | 12,6 s | 12,1 s | 0 |
| turbo | 113 s | 65 s | 0 |

No app do Mac (Swift, CPU com o Accelerate), o small leva 4,5 s e o turbo 10,4 s. No Windows/Linux, sem o BLAS
da Apple, o turbo leva o tempo da própria fala num M5 e mais que isso num PC comum: por isso o recomendado é o
small. Os tempos na CI dependem do push (`OPENFRAME_BENCH=1`).
### Threads e CPU ocupada

A barreira entre as operações do ggml (sem OpenMP, `ggml_barrier`) gira sem ceder a CPU. Com mais threads do whisper
que núcleos livres, uma thread que fica sem vez segura todas as outras, que continuam girando. Com 8 threads fixas, a
transcrição ficava de 2 a 25 vezes mais lenta num PC ocupado. Era o que fazia o `TranscriptionTests` levar 608 s com
dois `ctest -j8` na mesma máquina.

Agora, logo antes de transcrever (depois de carregar o modelo), o núcleo mede por 200 ms a CPU ocupada da máquina
inteira (`busyCores`: `host_statistics` no Mac, `/proc/stat` no Linux, `GetSystemTimes` no Windows) e usa os
núcleos livres, no mínimo min(4, máximo) e no máximo os núcleos menos 2, até 8 (`threadsFor`). Sem a medida, usa o
máximo, como antes. No Mac, os contadores da CPU são atualizados em lotes: às vezes ficam ~1 s parados, com a
máquina livre ou cheia (medido: 998 ms no máximo, em 20 s). Por isso, se nada andou na janela, a medida espera até
1,5 s a mais.

Medido no M5 (10 núcleos: 4 de desempenho e 6 de eficiência), modelo base, 65 s de fala, 2ª transcrição, duas
rodadas. A máquina tinha outros agentes rodando (load 6–7 mesmo "sem carga"):

| Carga em paralelo | Antes (8 threads) | Depois (automático → threads usadas) |
|---|---|---|
| nenhuma | 3,9–4,9 s | 4,0–4,3 s (8) |
| 4 `yes` | 9,7–12,9 s | 6,3–7,3 s (4–5) |
| 8 `yes` | 20,5–23 s | 8,9–9,3 s (4) |
| outro whisper com 8 threads fixas | 73–206 s | 14–20 s (4) |
| outro whisper automático | 19–24 s | 12,1–12,5 s (5–6) |

O cenário do `ctest` (dois `TranscriptionTests` inteiros ao mesmo tempo, com 4 `yes` no papel de outra
compilação, duas rodadas): antes, 170–201 s cada um (a transcrição longa do `silenceAndCancel` em 69–105 s); depois,
39–43 s (11–13 s). Os testes do whisper também deixaram de ter prazo próprio (veja "Variáveis de ambiente").

O que ficou de fora, medido:
- **Número fixo menor** (por exemplo, os 4 núcleos de desempenho): sem carga, 4 threads levam 35% a mais que 8
  (2,9 × 2,1 s numa rodada limpa) e 6 levam 15% a mais. Com carga, o melhor é sempre o número de núcleos livres.
  Nenhum número fixo acerta os dois casos.
- **Ceder a vez na barreira** (`sched_yield` depois de 16 mil voltas, ggml modificado): sem carga não muda nada
  (4,2 × 4,3 s), mas contra outro whisper sem a mudança ficou pior (167–197 s × 52–78 s). Quem cede perde a CPU
  para quem continua girando. Além disso, seria um remendo no código de terceiros.
- **Menos de 4 threads** com a CPU cheia: mais lento (8 `yes`: 4 threads 8,5–9 s, 2 threads 11,5 s, 1 thread
  19–20 s). O sistema divide o tempo entre todos os processos, e mais threads ganham mais fatias.
- **OpenMP** (a barreira dele gira um pouco e depois dorme): desligado de propósito, para não levar a
  libgomp/vcomp140 no pacote; o clang da Apple nem tem.

Tempos das palavras na fala com pausas conhecidas (modelo base): erro médio de 14 ms no início e no fim, com o
ajuste pela energia (sem ele, 0,31 s no DTW e 0,35 s nos tokens), como no Swift.

