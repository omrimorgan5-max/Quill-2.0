#!/usr/bin/env bash

set -u

TARGET="linux"
CLEAN=false

BIN_DIR="bin"
VERSION="2.3.0"

print_header() {
    echo
    echo "-----------------------------------------"
    echo "$1"
    echo "-----------------------------------------"
}

print_ok() {
    echo "[OK] $1"
}

print_fail() {
    echo "[FAIL] $1" >&2
}

build_cpp_binary() {
    local src="$1"
    local out="$2"
    local label="$3"

    echo "Building $label..."
    if [[ "$TARGET" == "linux" ]]; then
        g++ -std=c++17 "$src" src/core/transpiler/cli.cpp -O2 -o "$BIN_DIR/$out"
    else
        g++ -std=c++17 "$src" src/core/transpiler/cli.cpp -O2 -o "$BIN_DIR/quill-linux"
        x86_64-w64-mingw32-g++ -static -static-libgcc -static-libstdc++ "$src" src/core/transpiler/cli.cpp -O2 -o "$BIN_DIR/quill-windows64.exe"
        i686-w64-mingw32-g++ -static -static-libgcc -static-libstdc++ "$src" src/core/transpiler/cli.cpp -O2 -o "$BIN_DIR/quill-windows32.exe"


    fi
    if [ $? -ne 0 ]; then
        print_fail "Failed to build $label"
        return 1
    fi
    print_ok "$label -> $BIN_DIR/$out"
    return 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -t|--target)
            TARGET="${2:-linux}"
            shift 2
            ;;
        -c|--clean)
            CLEAN=true
            shift
            ;;
        -h|--help)
            echo "Usage: ./build.sh [-t linux|all] [-c]"
            exit 0
            ;;
        *)
            print_fail "Unknown option: $1"
            exit 1
            ;;
    esac
done

if [ "$CLEAN" = true ]; then
    print_header "Cleaning build artifacts"
    rm -rf "$BIN_DIR"
    print_ok "Removed $BIN_DIR"
    exit 0
fi

mkdir -p "$BIN_DIR"
print_header "Quill C++ Build v$VERSION"

case "$(echo "$TARGET" | tr '[:upper:]' '[:lower:]')" in
    linux)
        build_cpp_binary "src/core/transpiler/trans.cpp" "quill-linux" "transpiler" || exit 1
        ;;
    all)
        build_cpp_binary "src/core/transpiler/trans.cpp" "quill-linux" "transpiler" || exit 1
        ;;
    *)
        print_fail "Unsupported target: $TARGET"
        echo "Supported targets: linux, all"
        exit 1
        ;;
esac

print_header "Build Summary"
for file in "$BIN_DIR"/*; do
    if [ -f "$file" ]; then
        printf '%s\n' "$(basename "$file")"
    fi
done

echo
print_ok "Build complete"
