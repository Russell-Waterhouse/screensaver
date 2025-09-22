#!/bin/bash
set -e

rm -rf ./screensaver.bin

gcc -g3 screensaver.c xdg-shell-protocol.c -lwayland-client -o screensaver.bin

./screensaver.bin

