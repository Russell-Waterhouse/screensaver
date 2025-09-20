#!/bin/bash
set -e

rm -rf ./screensaver.bin

gcc -Werror -Wall screensaver.c -lwayland-client -o screensaver.bin

./screensaver.bin

