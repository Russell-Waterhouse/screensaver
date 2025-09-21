#!/bin/bash

set -e

mkdir protocols

cp /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml protocols/

wayland-scanner client-header protocols/xdg-shell.xml xdg-shell-client-protocol.h
wayland-scanner private-code protocols/xdg-shell.xml xdg-shell-protocol.c

