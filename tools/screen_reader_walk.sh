#!/bin/bash
# Walks a sample with Orca, as the screen readers walk them in the nightly
# run (docs/operations/test-environments.md):
#
#   tools/screen_reader_walk.sh <sample> <steps> <out-dir> [display]
#
# The sample, built with MAUL_UI_ATSPI and a window (sample_* of a
# MAUL_UI_WINDOW build), runs under Xvfb on a private session bus and
# runtime directory with the accessibility bus; Orca reads it with
# speech off, writing what it would say to its debug log; the steps go
# to the window through xdotool. <out-dir> receives speech.txt, a line
# per utterance, and Orca's whole log. <steps> holds a step a line:
#
#   key Tab           a key, pressed and let go
#   type hello        text typed, a key at a time
#   hold ctrl Left    a modifier held while a key goes, as a hand holds
#                     it: xdotool lets one go before the caret's event
#                     reaches Orca, which then reads Control alone
#   wait 2            seconds to wait
#   focus             the reader asked what has the focus, where it has
#                     such a command (NVDA's); nothing here
#   expect Notes      a phrase the reader must have said, in any case,
#                     after the phrase expected before it; every screen
#                     reader's walk checks it
#
# Lines starting with # are skipped. Each key and text waits 1.5 seconds
# for Orca. The display defaults to :143. Needs Xvfb, xdpyinfo,
# dbus-run-session, dbus-send, at-spi-bus-launcher, orca and xdotool.
set -eu
sample=$1
steps=$2
out=$3
display=${4:-:143}
mkdir -p "$out"
out=$(cd "$out" && pwd)
steps=$(cd "$(dirname "$steps")" && pwd)/$(basename "$steps")
bus=$(ls /usr/libexec/at-spi-bus-launcher /usr/lib/at-spi2-core/at-spi-bus-launcher 2>/dev/null | head -1)
export DISPLAY=$display
# Sockets of its own (speech-dispatcher, the accessibility bus), so no
# daemon outlives the walk or is shared with another.
XDG_RUNTIME_DIR=$(mktemp -d /tmp/screen-reader-walk.XXXXXX)
export XDG_RUNTIME_DIR
chmod 700 "$XDG_RUNTIME_DIR"
# Orca's settings: speech off, everything else its defaults.
mkdir -p "$XDG_RUNTIME_DIR/orca"
cat > "$XDG_RUNTIME_DIR/orca/user-settings.conf" <<'SETTINGS'
{"general": {"enableSpeech": false, "activeProfile": ["Default", "default"]},
 "profiles": {"default": {"profile": ["Default", "default"], "enableSpeech": false}},
 "pronunciations": {}, "keybindings": {}}
SETTINGS
# -noreset: without it the server resets whenever its last client leaves,
# as the readiness probe below does, and a connection arriving during the
# reset fails.
Xvfb "$display" -screen 0 1280x800x24 -noreset >/dev/null 2>&1 &
xvfb=$!
# The server is gone before the next walk takes its display.
trap 'kill $xvfb 2>/dev/null; wait $xvfb 2>/dev/null; rm -rf "$XDG_RUNTIME_DIR"' EXIT
# A loaded machine takes more than a moment to bring the server up, and
# Orca and the sample need it from their start: wait for it, at most 30
# seconds.
for i in $(seq 1 120); do
    xdpyinfo -display "$display" >/dev/null 2>&1 && break
    if ! kill -0 $xvfb 2>/dev/null || [ "$i" = 120 ]; then
        echo "Xvfb did not start on $display" >&2
        exit 2
    fi
    sleep 0.25
done
export WALK_SAMPLE=$sample WALK_STEPS=$steps WALK_OUT=$out WALK_BUS=$bus
dbus-run-session -- bash -c '
  "$WALK_BUS" --launch-immediately >/dev/null 2>&1 &
  launcher=$!
  # The accessibility bus answers before Orca starts, at most 30 seconds.
  for i in $(seq 1 120); do
      dbus-send --session --dest=org.a11y.Bus --print-reply /org/a11y/bus \
          org.a11y.Bus.GetAddress >/dev/null 2>&1 && break
      sleep 0.25
  done
  orca --replace -u "$XDG_RUNTIME_DIR/orca" --debug-file="$WALK_OUT/orca-debug.txt" \
      >"$WALK_OUT/orca-stdout.txt" 2>&1 &
  reader=$!
  for i in $(seq 1 30); do
      grep -q "SPEECH OUTPUT" "$WALK_OUT/orca-debug.txt" 2>/dev/null && break
      sleep 1
  done
  sleep 2
  "$WALK_SAMPLE" >"$WALK_OUT/sample.txt" 2>&1 &
  app=$!
  sleep 4
  window=$(xwininfo -root -children | awk "/^ +0x[0-9a-f]+ /{print \$1; exit}")
  xdotool windowfocus "$window"
  sleep 3
  while read -r verb rest; do
      case "$verb" in
      ""|"#"*) continue ;;
      key) xdotool key $rest; sleep 1.5 ;;
      type) xdotool type --delay 150 "$rest"; sleep 1.5 ;;
      hold)
          set -- $rest
          xdotool keydown "$1"; sleep 0.3; xdotool key "$2"; sleep 1.5
          xdotool keyup "$1"; sleep 0.5 ;;
      wait) sleep "$rest" ;;
      expect|focus) continue ;;
      *) echo "unknown step: $verb" >&2 ;;
      esac
      echo "$verb $rest" >>"$WALK_OUT/steps.txt"
  done <"$WALK_STEPS"
  sleep 2
  kill "$app" 2>/dev/null || true
  sleep 1
  # Orca'"'"'s debug file is buffered: a clean exit writes the rest.
  kill -TERM "$reader" 2>/dev/null || true
  for i in $(seq 1 20); do kill -0 "$reader" 2>/dev/null || break; sleep 0.5; done
  kill -9 "$reader" "$launcher" 2>/dev/null || true
  # What the bus started (its daemon, speech-dispatcher) lives in the
  # runtime directory.
  for p in $(ps -eo pid,args | awk -v d="$XDG_RUNTIME_DIR" "index(\$0, d) && !/awk/ {print \$1}"); do
      kill "$p" 2>/dev/null || true
  done
' 2>/dev/null
sed -n "s/.*SPEECH OUTPUT: '\(.*\)' {.*/\1/p" "$out/orca-debug.txt" >"$out/speech.txt"
cat "$out/speech.txt"
status=0
# Each phrase is looked for past the one before it.
said=$(cat "$out/speech.txt")
said=${said,,}
while read -r verb phrase; do
    [ "$verb" = expect ] || continue
    phrase=${phrase,,}
    case $said in
    *"$phrase"*) said=${said#*"$phrase"} ;;
    *)
        echo "not said in order: $phrase" >&2
        status=1
        ;;
    esac
done <"$steps"
exit $status
