#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
appdir=$(CDPATH= cd -- "${0%/*}" && pwd -P) || exit 1
exec "$appdir/tools/bridge/Kirby-Bridge" --appdir "$appdir" --external-launcher "$@"
