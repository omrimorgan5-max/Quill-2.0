#!/usr/bin/env bash

# ---- qpm Build Script -------------------------------------------------------
# Usage:
#   ./build.sh              - builds Windows binary (default)
#   ./build.sh linux        - builds Linux binary
#   ./build.sh all          - builds all platforms
#   ./build.sh --clean      - removes bin/ directory
# -----------------------------------------------------------------------------

# ---- Config -----------------------------------------------------------------

BIN_DIR="bin"
VERSION="0.3.0"
TARGET="linux"
CLEAN=false

# ---- Parse Arguments --------------------------------------------------------

while [[ $# -gt 0 ]]; do
    case "$1" in
        -c|--clean)
            CLEAN=true
            shift
            ;;
        -t|--target)
            TARGET="$2"
            shift 2
            ;;
        *)
            TARGET="$1"
            shift
            ;;
    esac
done

# ---- Helpers ----------------------------------------------------------------

write_header() {
    echo ""
    echo -e "\033[90m-----------------------------------------\033[0m"
    echo -e "\033[36m $1\033[0m"
    echo -e "\033[90m-----------------------------------------\033[0m"
}

write_success() {
    echo -e "\033[32m [OK] $1\033[0m"
}

write_fail() {
    echo -e "\033[31m [FAIL] $1\033[0m"
}

build_binary() {
    local os="$1"
    local arch="$2"
    local outfile="$3"

    echo -e "\033[90m  Building qpm...\033[0m"
    # qpm's Quill sources still use the removed C_top feature; build its
    # checked-in generated C snapshot until those sources are migrated.
    gcc -O2 -o ./bin/qpm ./cmd/qpm/qpm_main.c ./cmd/qpm/main.c -Wl,--allow-multiple-definition
    
    if [ $? -ne 0 ]; then
        write_fail "Failed to build qpm for $os/$arch"
        return 1
    fi
    
    write_success "qpm -> $BIN_DIR/$outfile"
    return 0
}

# ---- Clean ------------------------------------------------------------------

if [ "$CLEAN" = true ]; then
    write_header "Cleaning bin/"
    if [ -d "$BIN_DIR" ]; then
        rm -rf "$BIN_DIR"
        write_success "bin/ removed"
    else
        echo -e "\033[90m  bin/ does not exist, nothing to clean\033[0m"
    fi
    exit 0
fi

# ---- Setup ------------------------------------------------------------------

write_header "qpm Build v$VERSION"

if [ ! -d "$BIN_DIR" ]; then
    mkdir -p "$BIN_DIR"
    echo -e "\033[90m  Created bin/\033[0m"
fi

# ---- Builds -----------------------------------------------------------------

SUCCESS=true

case "$(echo "$TARGET" | tr '[:upper:]' '[:lower:]')" in

    linux)
        write_header "Building for Linux (x64)"
        build_binary "linux" "amd64" "qpm-linux" || SUCCESS=false
        ;;

    all)
        write_header "Building for all platforms"

        echo ""
        echo -e "\033[33m  Linux x64\033[0m"
        r2=true; build_binary "linux" "amd64" "qpm-linux" || r2=false

        if [ "$r1" = false ] || [ "$r2" = false ] || [ "$r3" = false ] || [ "$r4" = false ]; then
            SUCCESS=false
        fi
        ;;

    *)
        write_fail "Unknown target: $TARGET"
        echo -e "\033[90m  Valid targets: linux, all\033[0m"
        exit 1
        ;;
esac

# ---- Summary ----------------------------------------------------------------

write_header "Build Summary"

if [ "$SUCCESS" = true ]; then
    echo ""
    echo -e "\033[32m  All binaries built successfully\033[0m"
    echo ""
    echo -e "\033[90m  Output files:\033[0m"
    
    for file in "$BIN_DIR"/*; do
        if [ -f "$file" ]; then
            filename=$(basename "$file")
            # Calculate file size in MB with 2 decimal places
            size_mb=$(ls -l "$file" | awk '{printf "%.2f", $5/1048576}')
            echo -e "    $filename (${size_mb} MB)"
        fi
    done
    
    echo ""
    exit 0
else
    echo ""
    write_fail "Build completed with errors"
    echo ""
    exit 1
fi