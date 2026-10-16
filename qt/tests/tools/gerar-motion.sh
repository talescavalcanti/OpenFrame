#!/bin/bash
# Renderiza as animações, os loops e as legendas no núcleo Swift (FrameRenderer) com as fontes OFL que o Qt
# distribui e grava qt/tests/fixtures/motion.json, a referência do teste MotionReferenceTests da versão Qt.
# Só roda no macOS.
set -euo pipefail
cd "$(dirname "$0")/../../.."
BUILD=.build/motion
mkdir -p "$BUILD"
swiftc -O -o "$BUILD/openframe-motion" \
  $(find Sources/OpenFrameCore -name '*.swift' ! -name WhisperEngine.swift) \
  scripts/fixture/WhisperStub.swift qt/tests/tools/motion/main.swift
"$BUILD/openframe-motion" qt/src/core/fonts qt/src/app/fonts qt/tests/fixtures/motion.json
