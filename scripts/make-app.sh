#!/bin/bash
# Compila em modo release e monta build/OpenFrame.app, universal (Apple Silicon e Intel).
set -euo pipefail
cd "$(dirname "$0")/.."

# Sem Xcode, o `--arch arm64 --arch x86_64` do SwiftPM não funciona (pede o xcbuild):
# compila cada arquitetura com o próprio triple e junta com o lipo.
# No x86_64, o whisper.cpp (legendas automáticas) usa AVX2/FMA/F16C: todo Mac Intel que roda o
# macOS 14 tem essas instruções. Sem elas, o ggml cai no código escalar, bem mais lento.
BINS=()
for ARCH in arm64 x86_64; do
    TRIPLE="$ARCH-apple-macosx14.0"
    EXTRA=()
    if [ "$ARCH" = x86_64 ]; then
        EXTRA=(-Xcc -mavx -Xcc -mavx2 -Xcc -mfma -Xcc -mf16c)
    fi
    swift build -c release --product OpenFrame --triple "$TRIPLE" ${EXTRA[@]+"${EXTRA[@]}"}
    BINS+=("$(swift build -c release --triple "$TRIPLE" ${EXTRA[@]+"${EXTRA[@]}"} --show-bin-path)/OpenFrame")
done

APP=build/OpenFrame.app
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
lipo -create "${BINS[@]}" -output "$APP/Contents/MacOS/OpenFrame"
cp assets/brand/appicon/AppIcon.icns "$APP/Contents/Resources/AppIcon.icns"
# Licença do whisper.cpp/ggml (MIT), que vai compilado no binário.
mkdir -p "$APP/Contents/Resources/licencas"
cp Sources/CWhisper/LICENSE "$APP/Contents/Resources/licencas/whisper.cpp-LICENSE.txt"

cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>OpenFrame</string>
    <key>CFBundleDisplayName</key><string>OpenFrame</string>
    <key>CFBundleIdentifier</key><string>app.openframe.editor</string>
    <key>CFBundleExecutable</key><string>OpenFrame</string>
    <key>CFBundleIconFile</key><string>AppIcon</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>0.7.0</string>
    <key>CFBundleVersion</key><string>8</string>
    <key>LSMinimumSystemVersion</key><string>14.0</string>
    <key>LSApplicationCategoryType</key><string>public.app-category.video</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>NSMicrophoneUsageDescription</key><string>O OpenFrame usa o microfone para gravar narrações na timeline.</string>
    <key>NSPrincipalClass</key><string>NSApplication</string>
    <key>CFBundleDocumentTypes</key>
    <array>
        <dict>
            <key>CFBundleTypeName</key><string>Projeto OpenFrame</string>
            <key>CFBundleTypeRole</key><string>Editor</string>
            <key>LSHandlerRank</key><string>Owner</string>
            <key>CFBundleTypeExtensions</key><array><string>openframe</string></array>
        </dict>
    </array>
</dict>
</plist>
PLIST

codesign --force --sign - "$APP" >/dev/null
echo "Pronto: $APP"
