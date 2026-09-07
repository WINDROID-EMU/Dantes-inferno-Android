#!/usr/bin/env bash
set -euo pipefail

# ==============================================================================
# Dante's Inferno - Android ARM64 Build Script
# ==============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/out/build/android-arm64-release}"
ANDROID_DIR="$ROOT_DIR/android"

echo "=========================================================="
echo " Dante's Inferno - Compilação para Android ARM64 (AArch64)"
echo "=========================================================="

# Check Android NDK
if [[ -z "${ANDROID_NDK_ROOT:-}" ]]; then
    if [[ -n "${ANDROID_NDK_HOME:-}" ]]; then
        ANDROID_NDK_ROOT="$ANDROID_NDK_HOME"
    elif [[ -n "${ANDROID_HOME:-}" && -d "$ANDROID_HOME/ndk" ]]; then
        # Pick the latest NDK installed
        ANDROID_NDK_ROOT="$(find "$ANDROID_HOME/ndk" -mindepth 1 -maxdepth 1 -type d | sort -V | tail -n 1)"
    elif [[ -d "$HOME/Android/Sdk/ndk" ]]; then
        ANDROID_NDK_ROOT="$(find "$HOME/Android/Sdk/ndk" -mindepth 1 -maxdepth 1 -type d | sort -V | tail -n 1)"
    fi
fi

if [[ -z "${ANDROID_NDK_ROOT:-}" || ! -d "$ANDROID_NDK_ROOT" ]]; then
    echo "ERRO: ANDROID_NDK_ROOT não encontrado." >&2
    echo "Defina a variável ANDROID_NDK_ROOT apontando para o NDK r25+." >&2
    echo "Exemplo: export ANDROID_NDK_ROOT=\$HOME/Android/Sdk/ndk/26.1.10909125" >&2
    exit 1
fi

echo "-> NDK detectado: $ANDROID_NDK_ROOT"
TOOLCHAIN_FILE="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake"

if [[ ! -f "$TOOLCHAIN_FILE" ]]; then
    echo "ERRO: Toolchain cmake do NDK não encontrada em $TOOLCHAIN_FILE" >&2
    exit 1
fi

# 1. Compilação da biblioteca nativa (libdantes_inferno.so)
echo ""
echo "-> 1/2: Configurando e compilando bibliotecas nativas C++ (arm64-v8a)..."
mkdir -p "$BUILD_DIR"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-26 \
    -DANDROID_STL=c++_shared \
    -DCMAKE_BUILD_TYPE=Release \
    -DREXSDK_DIR="$ROOT_DIR/thirdparty/rexglue-sdk"

cmake --build "$BUILD_DIR" --target dantes_inferno --parallel "$(nproc)"

echo "-> Bibliotecas nativas compiladas com sucesso em $BUILD_DIR"

# 2. Empacotamento do APK via Gradle
echo ""
echo "-> 2/2: Montando APK Android..."
cd "$ANDROID_DIR"

if [[ -x "./gradlew" ]]; then
    ./gradlew assembleDebug
    echo ""
    echo "=========================================================="
    echo " SUCESSO: APK gerado em:"
    echo " $ANDROID_DIR/app/build/outputs/apk/debug/app-debug.apk"
    echo "=========================================================="
else
    echo "Aviso: gradlew não encontrado em $ANDROID_DIR."
    echo "Abra a pasta $ANDROID_DIR no Android Studio para gerar o APK assinado."
fi
