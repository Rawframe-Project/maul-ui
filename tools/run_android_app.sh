#!/bin/sh
# Runs a test application in the Android emulator, as
# Maul Window's runner does: installs it on the device ANDROID_SERIAL
# names (else the only one), starts its activity, and waits, a minute at
# most, for the closing line it writes to files/out, "result: N
# failures", read with run-as (the application is debuggable). The
# application never ends itself, so the runner stops it. Passes when the
# line says 0 failures; otherwise shows the application's crashes from
# the log.
#
# adb can lose its connection to the device ("error: closed"). The steps
# that only read the device's state, install, uninstall, remove the
# result file, clear the log, start or stop the activity are tried up to
# three times; the result line is read again until the minute is up. An
# answer the runner uses is read whole first, so a read that stays lost
# ends the run (set -e) rather than reading as an empty answer.
#
# Each run starts with the application fresh: one an earlier run left,
# as when its uninstall was lost, is uninstalled with its data first.
# The system handles a package's removal and install in its own time
# (MediaProvider orphans the documents a package owns then, those a
# test has just made among them), so the runner waits, 60 s at most,
# until the system has delivered its broadcasts, and does not clear the
# data with pm clear, whose broadcast arrives as late. The result file
# is then removed by name, and the device must answer that it is
# absent, so a result line an earlier run left cannot be read as this
# run's. The test fails when the system restarted while it ran, or when
# system_server's pid cannot be read after it. The verdict stands
# whether or not the stop and the uninstall go through.
set -eu
adb=${ADB:-adb}
apk=$1
package=$2

# An idempotent adb step, tried up to three times while adb fails.
again() {
    tries=1
    until "$adb" "$@"; do
        [ "$tries" -lt 3 ] || return 1
        tries=$((tries + 1))
        sleep 1
    done
}

# An idempotent adb step's output, tried up to three times while adb
# fails: only the try that succeeded is kept, so an answer a lost try
# printed before its connection dropped is not given twice.
ask() {
    tries=1
    until output=$("$adb" "$@"); do
        [ "$tries" -lt 3 ] || return 1
        tries=$((tries + 1))
        sleep 1
    done
    printf '%s\n' "$output"
}

start=$(date +%s)
installed=$(ask shell pm list packages "$package")
if printf '%s\n' "$installed" | tr -d '\r' | grep -qx "package:$package"; then
    again uninstall "$package" > /dev/null
fi
again install -r "$apk" > /dev/null
if ! timeout 60 "$adb" shell am wait-for-broadcast-idle > /dev/null; then
    echo "run_android_app.sh: the system's broadcasts were not seen idle in 60 s" >&2
fi
again shell run-as "$package" rm -f files/out
# The device tests for the file itself: a check that reads a failure as
# the file's absence would pass when run-as fails.
if ! left=$(ask shell "run-as $package sh -c 'if [ -e files/out ]; then echo present; else echo absent; fi'"); then
    echo "run_android_app.sh: no check that an earlier result is gone: it failed 3 times" >&2
    exit 1
fi
left=$(printf '%s' "$left" | tr -d '\r')
if [ "$left" != absent ]; then
    echo "run_android_app.sh: an earlier run's files/out may be left: the device answered '$left'" >&2
    exit 1
fi
server=$(ask shell pidof system_server)
server=$(printf '%s' "$server" | tr -d '\r')
again logcat -c
# The minute for the result counts from the start, whatever the steps
# before it took.
launched=$(date +%s)
again shell am start -W -n "$package/maul.ui.tests.TestActivity" > /dev/null
echo "started $package in $(($(date +%s) - start)) s"
out=$(mktemp)
while [ $(($(date +%s) - launched)) -lt 60 ]; do
    "$adb" shell run-as "$package" cat files/out > "$out" 2> /dev/null || true
    grep -q '^result: ' "$out" && break
    sleep 0.5
done
echo "waited until $(($(date +%s) - launched)) s after the start"
cat "$out"
# A system that restarted took the test with it; a pid that cannot be
# read, the system down or adb lost, fails the test as a restart would.
if server_now=$(ask shell pidof system_server); then
    server_now=$(printf '%s' "$server_now" | tr -d '\r')
else
    server_now=
fi
restarted=
if [ "$server_now" != "$server" ]; then
    restarted=1
    echo "run_android_app.sh: the system restarted during the test: system_server was pid $server, now ${server_now:-not read}" >&2
fi
status=1
if [ -z "$restarted" ] && grep -qx 'result: 0 failures' "$out"; then
    status=0
else
    "$adb" logcat -d -s AndroidRuntime:E libc:F ActivityManager:W | tail -60
    # Each native crash's tombstone whole, from its header on: the signal,
    # the fault address and the top frames come first, which a tail cuts.
    "$adb" logcat -d -s DEBUG:F | sed -n '/\*\*\* \*\*\* \*\*\*/,$p'
fi
again shell am force-stop "$package" || true
again uninstall "$package" > /dev/null || true
rm -f "$out"
exit $status
