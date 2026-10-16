#!/bin/bash
# Grava as falas dos testes de transcrição da versão Qt com o `say` (voz pt_BR Luciana), em WAV 16 kHz mono
# 16 bits, em qt/tests/fixtures/fala/. Só roda no macOS.
set -euo pipefail
cd "$(dirname "$0")/../../.."
BUILD=.build/fala
mkdir -p "$BUILD"
swiftc -O -o "$BUILD/openframe-fala" qt/tests/tools/fala/main.swift
"$BUILD/openframe-fala" qt/tests/fixtures/fala
