# Uma suíte Qt Test por arquivo: openframe_add_test(NomeTests NomeTests.cpp)
function(openframe_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE openframe_core Qt6::Test)
    target_compile_definitions(${name} PRIVATE OPENFRAME_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
    add_test(NAME ${name} COMMAND ${name})
    # Sem janela: o QFontDatabase e o QImage funcionam sem servidor gráfico (CI).
    # No Windows fica a plataforma nativa: o banco de fontes do offscreen depende
    # do contexto da plataforma "windows" e derruba os testes que consultam fontes.
    if(NOT WIN32)
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    else()
        # Sem um console (o ctest da CI), o Qt Test no Windows escreve tudo no OutputDebugString, e o log do
        # ctest fica vazio: assim a saída vai para o stdout.
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1")
        if(OPENFRAME_WHISPER_LIB_DIR AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
            # As DLLs do whisper/ggml ficam numa pasta própria do build.
            set_tests_properties(${name} PROPERTIES ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${OPENFRAME_WHISPER_LIB_DIR}")
        endif()
    endif()
endfunction()

openframe_add_test(TimelineTests TimelineTests.cpp)
openframe_add_test(ProjectTests ProjectTests.cpp)
openframe_add_test(SubtitleTests SubtitleTests.cpp)
openframe_add_test(ExportSettingsTests ExportSettingsTests.cpp)
openframe_add_test(ProjectLibraryTests ProjectLibraryTests.cpp)
openframe_add_test(FontCatalogTests FontCatalogTests.cpp)
openframe_add_test(MediaTests MediaTests.cpp)
openframe_add_test(RenderTests RenderTests.cpp)
openframe_add_test(TextTests TextTests.cpp)
openframe_add_test(CanvasLayoutTests CanvasLayoutTests.cpp)
openframe_add_test(CaptionTests CaptionTests.cpp)
openframe_add_test(TextLayoutTests TextLayoutTests.cpp)
openframe_add_test(MotionTests MotionTests.cpp)
openframe_add_test(CaptionRenderTests CaptionRenderTests.cpp)
openframe_add_test(TextSharpnessTests TextSharpnessTests.cpp)
openframe_add_test(MotionReferenceTests MotionReferenceTests.cpp)
openframe_add_test(AudioMixTests AudioMixTests.cpp)
openframe_add_test(AudioTimelineTests AudioTimelineTests.cpp)
openframe_add_test(WhisperUnitTests WhisperUnitTests.cpp)
target_link_libraries(WhisperUnitTests PRIVATE Qt6::Network)
openframe_add_test(TranscriptionTests TranscriptionTests.cpp)
openframe_add_test(WhisperVariantTests WhisperVariantTests.cpp)
# O whisper fica várias vezes mais lento com a CPU disputada: as esperas dos testes não têm prazo próprio. O
# único limite é o TIMEOUT do ctest (pega um travamento); o vigia do Qt Test (5 min por função) e as esperas usam o
# mesmo valor (OPENFRAME_TEST_TIMEOUT_MS).
set(whisper_test_timeout 900)
foreach(t TranscriptionTests WhisperVariantTests)
    # OPENFRAME_GGML_DL e OPENFRAME_GGML_BUILD_DIR (a pasta das variantes).
    if(TARGET openframe::whisper)
        target_link_libraries(${t} PRIVATE openframe::whisper)
    else()
        target_compile_definitions(${t} PRIVATE "OPENFRAME_GGML_BUILD_DIR=\"\"" "OPENFRAME_BUILD_ROOT=\"\"")
    endif()
    target_compile_definitions(${t} PRIVATE OPENFRAME_TEST_TIMEOUT_MS=${whisper_test_timeout}000)
    set_tests_properties(${t} PROPERTIES TIMEOUT ${whisper_test_timeout})
    set_property(TEST ${t} APPEND PROPERTY ENVIRONMENT "QTEST_FUNCTION_TIMEOUT=${whisper_test_timeout}000")
endforeach()
openframe_add_test(PlayerTests PlayerTests.cpp)
openframe_add_test(ExportTests ExportTests.cpp)
# Lê o arquivo exportado direto pelo FFmpeg (codec, fps, marcas de cor).
include(FFmpeg)
target_link_libraries(ExportTests PRIVATE FFmpeg::FFmpeg)
set_tests_properties(ExportTests PROPERTIES TIMEOUT 1500)
set_tests_properties(PlayerTests PROPERTIES TIMEOUT 900)

# Fora do Mac, as fontes do Mac viram a Inter e as outras livres (FontCatalog::resolve), um pouco mais largas: a
# segmentação das legendas e as quebras de linha mudam. No Mac, estas rodam de novo só com as fontes distribuídas,
# para pegar aqui o que a CI do Windows e do Linux pegaria.
if(APPLE)
    foreach(t CaptionTests CaptionRenderTests TextLayoutTests)
        add_test(NAME ${t}BundledFonts COMMAND ${t})
        set_tests_properties(${t}BundledFonts PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;OPENFRAME_BUNDLED_FONTS_ONLY=1")
    endforeach()
    set_tests_properties(CaptionRenderTestsBundledFonts PROPERTIES RESOURCE_LOCK heavy)
endif()

# Os que disputam CPU ou medem tempo um de cada vez, como o HeavyWork do Swift: o whisper, a comparação com o
# Swift, a exportação, o player e as medições de desempenho.
set_tests_properties(TranscriptionTests WhisperVariantTests MotionReferenceTests ExportTests PlayerTests
    MotionTests CaptionRenderTests TextSharpnessTests PROPERTIES RESOURCE_LOCK heavy)
