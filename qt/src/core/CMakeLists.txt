# Núcleo sem interface: modelo, timeline, mídia (FFmpeg), render e exportação.
add_library(openframe_core STATIC
    Timing.h Timing.cpp
    Enums.h
    Color.h
    Captions.h
    Effects.h Effects.cpp
    Project.h Project.cpp
    ProjectStore.h ProjectStore.cpp
    TimelineEditor.h TimelineEditor.cpp
    CanvasLayout.h CanvasLayout.cpp
    CaptionPresets.h CaptionPresets.cpp
    CaptionBuilder.h CaptionBuilder.cpp
    WhisperModels.h WhisperModels.cpp
    Transcriber.h Transcriber.cpp
    WordAligner.h WordAligner.cpp
    AudioMixdown.h AudioMixdown.cpp
    WhisperEngine.h WhisperEngine.cpp
    Subtitles.h Subtitles.cpp
    ExportSettings.h ExportSettings.cpp
    ProjectLibrary.h ProjectLibrary.cpp
    FontCatalog.h FontCatalog.cpp
    FFmpeg.h FFmpeg.cpp
    MediaProbe.h MediaProbe.cpp
    VideoDecoder.h VideoDecoder.cpp
    ToneMap.h ToneMap.cpp
    Thumbnails.h Thumbnails.cpp
    Waveform.h Waveform.cpp
    MediaWriter.h MediaWriter.cpp
    LinearImage.h LinearImage.cpp
    FilterModels.h FilterModels.cpp
    ImageEffects.h ImageEffects.cpp
    Motion.h Motion.cpp
    TextShapes.h
    TextRenderer.h TextRenderer.cpp
    TextMotion.h TextMotion.cpp
    AnimationPreview.h AnimationPreview.cpp
    Renderer.h Renderer.cpp
    AudioMix.h AudioMix.cpp
    AudioRecorder.h AudioRecorder.cpp
    Player.h Player.cpp
    Exporter.h Exporter.cpp
)
target_include_directories(openframe_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# Fontes livres (OFL) que substituem as do Mac fora dele (FontCatalog::resolve). Licenças em fonts/OFL-*.txt.
qt_add_resources(openframe_core "openframe_fonts"
    PREFIX "/openframe"
    FILES
        fonts/Inter-Regular.ttf
        fonts/Inter-Light.ttf
        fonts/Inter-Medium.ttf
        fonts/Inter-Bold.ttf
        fonts/JetBrainsMono-Regular.ttf
        fonts/JetBrainsMono-Bold.ttf
        fonts/PlayfairDisplay-Italic.ttf
        fonts/BarlowCondensed-Bold.ttf
        fonts/BarlowCondensed-Black.ttf
)
target_link_libraries(openframe_core PUBLIC Qt6::Core Qt6::Gui Qt6::Concurrent)

# Saída de som do Player (QAudioSink).
find_package(Qt6 REQUIRED COMPONENTS Multimedia)
target_link_libraries(openframe_core PRIVATE Qt6::Multimedia)

include(FFmpeg)
target_link_libraries(openframe_core PRIVATE FFmpeg::FFmpeg)

# Transcrição (whisper.cpp na CPU) e download dos modelos (o único acesso à rede, só quando o usuário pede).
include(Whisper)
if(TARGET openframe::whisper)
    target_link_libraries(openframe_core PRIVATE openframe::whisper)
endif()
target_link_libraries(openframe_core PRIVATE Qt6::Network)
