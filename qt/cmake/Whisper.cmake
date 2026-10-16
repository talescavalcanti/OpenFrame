# whisper.cpp (com o ggml) para as legendas automáticas: a mesma versão do Mac (v1.9.5,
# d1be6fde11ac6e0407606b4e42fe72d34add8037), só o backend CPU.
#
# Vem por FetchContent, do tarball do commit com o sha256 conferido, e não da cópia do Mac em
# Sources/CWhisper: aquela cópia só tem o que o Mac usa (sem o CMake do ggml e sem os arquivos x86), e é o
# CMake do ggml que monta as variantes de CPU. Para compilar sem rede, aponte
# FETCHCONTENT_SOURCE_DIR_WHISPER para uma cópia do mesmo commit.
#
# CPUs: com OPENFRAME_WHISPER_VARIANTS (padrão), o backend CPU sai em várias bibliotecas, uma por conjunto de
# instruções (x64, sse42, sandybridge, haswell, skylakex, icelake, alderlake… no x86; apple_m1, apple_m2_m3,
# apple_m4 no Mac; armv8.x/9.x no Linux ARM), e o app carrega a melhor que a CPU suporta
# (GGML_BACKEND_DL + GGML_CPU_ALL_VARIANTS): nada de instrução ilegal num PC sem AVX2. Todas as bibliotecas
# do whisper/ggml vão para OPENFRAME_WHISPER_LIB_DIR; a lista do pacote está no API.md.
#
# Define o alvo openframe::whisper e, no núcleo, OPENFRAME_HAS_WHISPER (e OPENFRAME_GGML_DL com as variantes).
option(OPENFRAME_WHISPER "Compila a transcrição com o whisper.cpp" ON)
option(OPENFRAME_WHISPER_VARIANTS "Variantes do backend CPU escolhidas em tempo de execução" ON)
set(OPENFRAME_WHISPER_LIB_DIR "${CMAKE_BINARY_DIR}/ggml" CACHE PATH "Pasta das bibliotecas do whisper/ggml")

if(OPENFRAME_WHISPER AND NOT TARGET openframe::whisper)
    include(FetchContent)

    function(openframe_fetch_whisper)
        # Opções do whisper/ggml só neste escopo (o resto do projeto não muda).
        set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
        set(WHISPER_BUILD_TESTS OFF)
        set(WHISPER_BUILD_EXAMPLES OFF)
        set(WHISPER_BUILD_SERVER OFF)
        set(WHISPER_CURL OFF)
        set(WHISPER_SDL2 OFF)
        set(WHISPER_ALL_WARNINGS OFF)
        set(GGML_METAL OFF)
        set(GGML_CUDA OFF)
        set(GGML_VULKAN OFF)
        set(GGML_BLAS OFF)
        # O pool de threads do próprio ggml: sem a libgomp/vcomp140 no pacote.
        set(GGML_OPENMP OFF)
        set(GGML_CCACHE OFF)
        # Nunca otimizar para a CPU da máquina que compila: o pacote roda em outras.
        set(GGML_NATIVE OFF)
        if(OPENFRAME_WHISPER_VARIANTS)
            set(BUILD_SHARED_LIBS ON)
            set(GGML_BACKEND_DL ON)
            set(GGML_CPU_ALL_VARIANTS ON)
        else()
            set(BUILD_SHARED_LIBS OFF)
        endif()
        # Código de terceiros: sem os avisos que o projeto liga (-Wall -Wextra) e, num build sem otimização (o
        # padrão sem CMAKE_BUILD_TYPE, ou Debug), com -O2 mesmo assim: sem ela, transcrever leva minutos.
        foreach(lang C CXX)
            string(REGEX REPLACE "(^| )-W(all|extra)" "" CMAKE_${lang}_FLAGS "${CMAKE_${lang}_FLAGS}")
            if(NOT MSVC AND (NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "Debug"))
                string(APPEND CMAKE_${lang}_FLAGS " -O2")
            endif()
        endforeach()
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OPENFRAME_WHISPER_LIB_DIR}")
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${OPENFRAME_WHISPER_LIB_DIR}")
        foreach(config Debug Release RelWithDebInfo MinSizeRel)
            string(TOUPPER ${config} upper)
            set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${upper} "${OPENFRAME_WHISPER_LIB_DIR}")
            set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${upper} "${OPENFRAME_WHISPER_LIB_DIR}")
        endforeach()

        # Fora do ALL (EXCLUDE_FROM_ALL): só compila o que o OpenFrame usa (o Parakeet, outro reconhecedor que vem
        # nesta versão, fica de fora), e as regras de instalação do whisper/ggml (cabeçalhos, pkg-config, CMake,
        # o Parakeet) não entram no `cmake --install`, que instala só o app. O pacote leva as bibliotecas e as
        # variantes copiando OPENFRAME_WHISPER_LIB_DIR (API.md, "Arquivos que o pacote leva").
        set(source
            URL https://github.com/ggml-org/whisper.cpp/archive/d1be6fde11ac6e0407606b4e42fe72d34add8037.tar.gz
            URL_HASH SHA256=645445da884df6dbd52b30f82d7be7c01d1767c938714e477e786b88d529e9f4
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
            FetchContent_Declare(whisper ${source} EXCLUDE_FROM_ALL)
            FetchContent_MakeAvailable(whisper)
        else()
            FetchContent_Declare(whisper ${source})
            FetchContent_GetProperties(whisper)
            if(NOT whisper_POPULATED)
                FetchContent_Populate(whisper)
                add_subdirectory(${whisper_SOURCE_DIR} ${whisper_BINARY_DIR} EXCLUDE_FROM_ALL)
            endif()
        endif()
        # As variantes de CPU são carregadas com dlopen (ninguém liga com elas): sem isto, fora do ALL, nem
        # compilariam.
        get_property(targets DIRECTORY ${whisper_SOURCE_DIR}/ggml/src PROPERTY BUILDSYSTEM_TARGETS)
        list(FILTER targets INCLUDE REGEX "^ggml-cpu")
        add_custom_target(openframe_ggml_cpu ALL)
        if(targets)
            add_dependencies(openframe_ggml_cpu ${targets})
        endif()
    endfunction()
    openframe_fetch_whisper()

    add_library(openframe_whisper INTERFACE)
    add_library(openframe::whisper ALIAS openframe_whisper)
    target_link_libraries(openframe_whisper INTERFACE whisper ggml ggml-base)
    # Quem liga com o whisper (o app, os testes) espera as variantes: elas são carregadas da pasta do build.
    add_dependencies(openframe_whisper openframe_ggml_cpu)
    target_compile_definitions(openframe_whisper INTERFACE OPENFRAME_HAS_WHISPER
        "OPENFRAME_GGML_BUILD_DIR=\"${OPENFRAME_WHISPER_LIB_DIR}\""
        # A pasta do build só vale para um executável dentro desta árvore (nunca num pacote).
        "OPENFRAME_BUILD_ROOT=\"${CMAKE_BINARY_DIR}\"")
    if(OPENFRAME_WHISPER_VARIANTS)
        target_compile_definitions(openframe_whisper INTERFACE OPENFRAME_GGML_DL)
    endif()
endif()

# Copia as bibliotecas do whisper/ggml (com as variantes) para a pasta de `target` depois do build, para ele
# rodar direto do build no Windows (onde as DLLs têm de estar ao lado do executável). O pacote leva as mesmas.
function(openframe_deploy_whisper target)
    if(TARGET openframe::whisper AND WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${OPENFRAME_WHISPER_LIB_DIR}" "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    endif()
endfunction()

