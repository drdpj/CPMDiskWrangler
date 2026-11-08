#!/bin/bash
# run_cli.sh [--update] <tool binary> <work dir> [<real data dir>]
#
# Golden-image tests of the command-line program (acorndw / cpmdw). One case = one folder under cases/:
#   setup      shell lines that make the input in $W (the case's own work folder; cwd). A line "# needs-real" anywhere
#              in it marks a case that runs only when the real data folder is given ($REAL); "# needs-env NAME" one that runs only
#              when that variable is set (TD0_DIR: a folder of TeleDisk files). Also set: $TOOL, $W, $CASE, $TESTS (the
#              app's tests/ folder), $AS_USER (a prefix that runs the tool as an unprivileged user when this is root).
#   cmds       one invocation of the tool per line ($TOOL ... --json; blank lines and # lines ignored); line N's stdout is
#              compared with expected/N.json and its exit code with expected/N.exit
#   host/      fixture files, copied to $W/host (dates set to 2000-01-01 00:00:00)
#   verify     optional shell lines run after cmds in $W (cmp and the like); failing it fails the case
#   after      optional: tool invocations whose stdout becomes expected/after.json (for a container or a reading that the
#              plain image hash can't show); without it the image files $W/img* are hashed into expected/image.sha256
#   expect-unchanged   optional marker: the image files must be byte-identical to what setup made
#   cleanup    optional: shell lines run last in $W (a real case deletes its big image copy here)
#   expected/  N.json, N.exit, image.sha256 or after.json, and hostfiles.txt ("sha256  path", sorted) when $W/out exists
# Every run is deterministic: SOURCE_DATE_EPOCH=946684800, TZ=UTC, fixtures touched to 2000-01-01.
# --update rewrites expected/ from what the tool does now: only for the person changing the program's behaviour, in a
# commit of its own that says why.
# Ends "ALL PASSED" or "FAILED:" and the names of the cases.
UPDATE=0
[ "$1" = --update ] && { UPDATE=1; shift; }
[ $# -ge 2 ] || { echo "usage: $0 [--update] <tool binary> <work dir> [<real data dir>]"; exit 2; }
HERE=$(cd "$(dirname "$0")" && pwd)
TOOLSRC=$(readlink -f "$1"); WORK=$2; REAL=""
[ -n "$3" ] && REAL=$(cd "$3" && pwd)
[ -x "$TOOLSRC" ] || { echo "$TOOLSRC: not an executable"; exit 2; }
chmod -R u+w "$WORK" 2>/dev/null; rm -rf "$WORK"; mkdir -p "$WORK/bin"; WORK=$(cd "$WORK" && pwd); chmod 755 "$WORK" "$WORK/bin"
cp "$TOOLSRC" "$WORK/bin/"; export TOOL=$WORK/bin/$(basename "$TOOLSRC")
export SOURCE_DATE_EPOCH=946684800 TZ=UTC LC_ALL=C.UTF-8 QT_QPA_PLATFORM=offscreen
# a read-only folder stops root too only when the tool runs as someone else
AS_USER=""
if [ "$(id -u)" = 0 ] && command -v setpriv >/dev/null 2>&1; then AS_USER="setpriv --reuid=65534 --regid=65534 --clear-groups"; fi
export AS_USER REAL TESTS=$(dirname "$HERE")
fail=(); npass=0; nskip=0
for c in "$HERE"/cases/*/; do
  c=${c%/}; name=$(basename "$c")
  if grep -q '^# needs-real' "$c/setup" 2>/dev/null && [ -z "$REAL" ]; then echo "skip $name (needs the real data folder)"; nskip=$((nskip+1)); continue; fi
  need=$(sed -n 's/^# needs-env //p' "$c/setup" 2>/dev/null | head -1)
  if [ -n "$need" ] && [ -z "${!need}" ]; then echo "skip $name (needs \$$need)"; nskip=$((nskip+1)); continue; fi
  W=$WORK/cases/$name; A=$WORK/actual/$name; mkdir -p "$W" "$A"; chmod 755 "$W"
  export W CASE=$c
  [ -d "$c/host" ] && cp -r "$c/host" "$W/host"
  find "$W" -exec touch -d '2000-01-01 00:00:00' {} +
  why=""
  if [ -f "$c/setup" ]; then (cd "$W" && bash -e "$c/setup") >"$A/setup.log" 2>&1 || why="setup failed (see $A/setup.log)"; fi
  if [ -z "$why" ]; then
    (cd "$W" && sha256sum img* 2>/dev/null) >"$A/setup.sha"
    n=0
    while IFS= read -r line || [ -n "$line" ]; do
      case "$line" in ''|'#'*) continue;; esac
      n=$((n+1))
      (cd "$W" && eval "$line") >"$A/$n.json" 2>"$A/$n.err"; echo $? >"$A/$n.exit"
    done <"$c/cmds"
    [ -f "$c/verify" ] && { (cd "$W" && bash -e "$c/verify") >"$A/verify.log" 2>&1 || why="verify failed (see $A/verify.log)"; }
    if [ -f "$c/after" ]; then
      : >"$A/after.json"
      while IFS= read -r line || [ -n "$line" ]; do
        case "$line" in ''|'#'*) continue;; esac
        (cd "$W" && eval "$line") >>"$A/after.json" 2>/dev/null
      done <"$c/after"
    else
      (cd "$W" && sha256sum img* 2>/dev/null) >"$A/image.sha256"
    fi
    if [ -d "$W/out" ]; then (cd "$W/out" && find . -type f | sed 's|^\./||' | LC_ALL=C sort | while IFS= read -r f; do sha256sum -- "$f"; done) >"$A/hostfiles.txt"; fi
    [ -f "$c/cleanup" ] && (cd "$W" && bash -e "$c/cleanup") >"$A/cleanup.log" 2>&1
    left=$(cd "$W" && find . -name '.dw*' | head -3)
    [ -n "$left" ] && why="temporary files left behind: $left"
    if [ -f "$c/expect-unchanged" ] && ! cmp -s "$A/setup.sha" "$A/image.sha256"; then why="the image changed, and this case says it must not"; fi
  fi
  if [ $UPDATE = 1 ] && [ -z "$why" ]; then
    rm -rf "$c/expected"; mkdir -p "$c/expected"
    for f in "$A"/*.json "$A"/*.exit "$A"/image.sha256 "$A"/after.json "$A"/hostfiles.txt; do [ -f "$f" ] && cp "$f" "$c/expected/"; done
    echo "updated $name"; continue
  fi
  if [ -z "$why" ]; then
    for f in "$c"/expected/*; do
      b=$(basename "$f")
      [ -f "$A/$b" ] || { why="expected $b was not produced"; break; }
      cmp -s "$f" "$A/$b" || { why="$b differs: $(diff -u "$f" "$A/$b" | sed -n '3,12p' | tr '\n' '|')"; break; }
    done
  fi
  if [ -z "$why" ]; then
    for f in "$A"/*.json "$A"/*.exit "$A"/image.sha256 "$A"/after.json "$A"/hostfiles.txt; do
      [ -f "$f" ] && [ ! -f "$c/expected/$(basename "$f")" ] && { why="$(basename "$f") has no expectation"; break; }
    done
  fi
  if [ -z "$why" ]; then echo "pass $name"; npass=$((npass+1)); else echo "FAIL $name: $why"; fail+=("$name"); fi
done
echo "$npass passed, ${#fail[@]} failed, $nskip skipped"
if [ ${#fail[@]} -eq 0 ]; then echo "ALL PASSED"; else echo "FAILED: ${fail[*]}"; exit 1; fi
