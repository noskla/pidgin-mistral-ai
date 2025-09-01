#!/bin/sh
set -e

check_command() {
    local cmd=$1
    local pkg=$2
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "Error: $cmd is not found in PATH."
        echo "       Please install $pkg."
        exit 1
    fi
}

script_dir=$(dirname "$(readlink -f "$0")")
cd "$script_dir"

check_command autoconf autoconf
check_command automake automake
check_command libtool libtool
check_command pkg-config pkg-config

mkdir -p m4

if [ ! -f "plugins.mk" ]; then
    cat > plugins.mk << 'EOF'
# Minimal plugin build rules for standalone Pidgin plugin builds

# Define how to install plugin libraries
install-pluginLTLIBRARIES: install-libLTLIBRARIES

# Standard flags for plugin libraries
AM_LDFLAGS = -module -avoid-version
AM_LIBTOOLFLAGS = --tag=disable-static
EOF
fi

echo "Running autoreconf..."
autoreconf --force --install --verbose

if [ -z "$NOCONFIGURE" ]; then
    echo "Running configure..."
    ./configure "$@"
fi
