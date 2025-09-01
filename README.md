# Pidgin Mistral AI account protocol

A Pidgin protocol to integrate with Mistral AI.

This is just a toy, not production ready, I'm not responsible for any errors.

## Prerequisites

On Debian/Ubuntu, install the following packages:

```bash
sudo apt-get install autoconf automake libtool libtool-bin pkg-config libpurple-dev libjson-glib-dev libcurl4-openssl-dev libgtk-3-dev
```

## Installation

1. Clone this repository.
2. Run the following commands to compile and install the plugin:

```bash
chmod +x autogen.sh
./autogen.sh
./configure --prefix=/usr
make
sudo make install
```
