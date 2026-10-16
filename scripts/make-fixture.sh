#!/bin/bash
# Gera os projetos de referência que a versão multiplataforma (qt/) usa nos testes
# de compatibilidade do formato .openframe: completo, fundo-preto, fundo-desfoque,
# motion (todas as animações e loops), legendas (palavras, destaque, palavra-chave e
# faixa automática) e audio (som extraído, gravação e altura das faixas).
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD=.build/fixture
mkdir -p "$BUILD" qt/tests/fixtures
# O WhisperEngine depende do whisper.cpp (alvo CWhisper): entra um substituto que não transcreve.
swiftc -O -o "$BUILD/openframe-fixture" \
  $(find Sources/OpenFrameCore -name '*.swift' ! -name WhisperEngine.swift) \
  scripts/fixture/WhisperStub.swift scripts/fixture/main.swift
"$BUILD/openframe-fixture" qt/tests/fixtures/completo.openframe
