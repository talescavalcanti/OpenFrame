#!/bin/bash
# Mede a caixa do texto (TextRenderer.boxSize) no núcleo Swift, com as fontes Inter que o Qt distribui,
# e grava qt/tests/fixtures/caixas.json, a referência do teste TextLayoutTests da versão Qt. Só roda no macOS.
set -euo pipefail
cd "$(dirname "$0")/../../.."
BUILD=.build/caixas
mkdir -p "$BUILD"
swiftc -O -o "$BUILD/openframe-caixas" \
  $(find Sources/OpenFrameCore -name '*.swift' ! -name WhisperEngine.swift) \
  scripts/fixture/WhisperStub.swift qt/tests/tools/caixas/main.swift
"$BUILD/openframe-caixas" qt/src/core/fonts qt/src/app/fonts qt/tests/fixtures/caixas.json
