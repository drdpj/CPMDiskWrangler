#!/bin/bash
# CP/M Disk Wrangler - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
# cross_tool.sh <cpmdw> <acorndw> <work dir>: the two command-line programs hand files to each other through
# 'get --manifest' and 'put --retro' (the retro-files manifest), with the CP/M user numbers and the Acorn load and
# execution addresses carried across. Exit status 0 only if every step held.
C=$(readlink -f "${1:?usage: cross_tool.sh <cpmdw> <acorndw> <work dir>}"); A=$(readlink -f "${2:?}"); W=$3
export SOURCE_DATE_EPOCH=946684800 TZ=UTC LC_ALL=C.UTF-8 QT_QPA_PLATFORM=offscreen
rm -rf "$W"; mkdir -p "$W/host" && cd "$W" || exit 1
fail=0; ck() { if "$@" >/dev/null 2>&1; then echo "ok   $*"; else echo "FAIL $*"; fail=1; fi; }
head -c 3000 /dev/urandom > host/GAME.COM; printf 'text\r\n\x1a' > host/NOTE.TXT
F=--format=ibm-3740
"$C" new c.img $F --quiet && "$C" put c.img host/GAME.COM / $F --quiet && "$C" put c.img host/NOTE.TXT 3: $F --quiet || fail=1
# CP/M -> Acorn: every user area becomes a directory, the file contents are the same
"$C" get c.img '*:*' out1 --manifest=out1/m.json $F --quiet
"$A" new a.adl --format=adfs-m --quiet && "$A" put a.adl / --retro=out1/m.json --quiet
ck "$A" check a.adl --strict
"$A" get a.adl '$.user0.GAME_COM' '$.user3.NOTE_TXT' back1 --user-folders=off --quiet 2>/dev/null || "$A" get a.adl '$' back1 --quiet
ck cmp host/GAME.COM "$(find back1 -iname 'game_com' | head -1)"
ck cmp host/NOTE.TXT "$(find back1 -iname 'note_txt' | head -1)"
# Acorn -> CP/M: a manifest from acorndw lands on a CP/M disc (names made CP/M-legal, one user area)
"$A" new d.ssd --format=dfs-ss40 --quiet && "$A" put d.ssd host/GAME.COM / --quiet
"$A" get d.ssd '*.*' out2 --manifest=out2/m.json --quiet
"$C" new e.img $F --quiet && "$C" put e.img / --retro=out2/m.json $F --quiet
ck "$C" check e.img $F --strict
n=$("$C" ls e.img '*:' $F --json | python3 -I -c 'import json,sys; print(len(json.load(sys.stdin)["result"]["entries"]))')
[ "$n" = 1 ] && echo "ok   one file landed on the CP/M disc" || { echo "FAIL $n files on the CP/M disc"; fail=1; }
"$C" get e.img '*:*' back2 $F --quiet
ck cmp host/GAME.COM "$(find back2 -type f | head -1)"
[ $fail -eq 0 ] && echo "cross_tool: all ok" || echo "cross_tool: FAILED"
exit $fail
