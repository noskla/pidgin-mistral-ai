#!/bin/bash
set -e

PACKAGE_NAME="pidgin-mistral-ai"
VERSION="1.0.0"
ARCHITECTURE="amd64"
MAINTAINER="Yaro <yaroslav.cichocki@proton.me>"
DESCRIPTION="Pidgin protocol plugin to integrate with Mistral AI"

BUILD_DIR="build"
DEB_DIR="$BUILD_DIR/deb"
INSTALL_DIR="$DEB_DIR/usr"
CONTROL_DIR="$DEB_DIR/DEBIAN"

mkdir -p "$BUILD_DIR"
mkdir -p "$INSTALL_DIR"
mkdir -p "$CONTROL_DIR"

DEPENDENCIES="autoconf automake libtool libtool-bin pkg-config libpurple-dev libjson-glib-dev libcurl4-openssl-dev libgtk-3-dev"
MISSING_DEPS=""

for dep in $DEPENDENCIES; do
    if ! dpkg -l | grep -qw "$dep"; then
        MISSING_DEPS="$MISSING_DEPS $dep"
    fi
done

if [ -n "$MISSING_DEPS" ]; then
    echo "Installing missing dependencies: $MISSING_DEPS"
    sudo apt-get update
    sudo apt-get install -y $MISSING_DEPS
else
    echo "All dependencies are already installed."
fi

echo "Compiling the plugin..."
chmod +x autogen.sh
./autogen.sh
./configure --prefix="$(pwd)/$INSTALL_DIR"
make
make install DESTDIR="$(pwd)/$DEB_DIR"

echo "Creating Debian package structure..."

cat > "$CONTROL_DIR/control" << EOF
Package: $PACKAGE_NAME
Version: $VERSION
Section: net
Priority: optional
Architecture: $ARCHITECTURE
Maintainer: $MAINTAINER
Description: $DESCRIPTION
Depends: libpurple-dev, libjson-glib-dev, libcurl4-openssl-dev, libgtk-3-dev
EOF

chmod 755 "$DEB_DIR"
chmod 644 "$CONTROL_DIR/control"

echo "Building .deb package..."
dpkg-deb --build "$DEB_DIR" "${PACKAGE_NAME}_${VERSION}_${ARCHITECTURE}.deb"

echo "Package created: ${PACKAGE_NAME}_${VERSION}_${ARCHITECTURE}.deb"
