#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ ! -x "$root/build/EnglishAssistantCore" ]; then
    printf '%s\n' '请先运行：sh ./install.sh'
    exit 1
fi
exec /usr/bin/python3 "$root/linux/app.py" "$@"
