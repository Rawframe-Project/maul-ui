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
# adb can lose its connection to the device ("error: closed"). Installing,
# clearing the application's data and the log, removing its result file,
# starting the activity, stopping it and uninstalling are idempotent, so
# each is tried up to three times; the result line is read again until
# the minute is up.
#
# Before the start the data is cleared, the result file is removed by
# name, and the device must answer that the file is absent, since some
# Android versions answer a failed pm clear with "Failed" and status 0.
# So a result line an earlier run left (its uninstall lost) cannot be
# read as this run's. The verdict stands whether or not the stop and the
# uninstall go through.
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

start=$(date +%s)
again install -r "$apk" > /dev/null
again shell pm clear "$package" > /dev/null
again shell run-as "$package" rm -f files/out
# The device tests for the file itself: a check that reads a failure as
# the file's absence would pass when run-as fails.
if ! left=$(again shell "run-as $package sh -c 'if [ -e files/out ]; then echo present; else echo absent; fi'"); then
    echo "run_android_app.sh: no check that an earlier result is gone: it failed 3 times" >&2
    exit 1
fi
left=$(printf '%s' "$left" | tr -d '\r')
if [ "$left" != absent ]; then
    echo "run_android_app.sh: an earlier run's files/out may be left: the device answered '$left'" >&2
    exit 1
fi
again logcat -c
again shell am start -W -n "$package/maul.ui.tests.TestActivity" > /dev/null
echo "started $package in $(($(date +%s) - start)) s"
out=$(mktemp)
while [ $(($(date +%s) - start)) -lt 60 ]; do
    "$adb" shell run-as "$package" cat files/out > "$out" 2> /dev/null || true
    grep -q '^result: ' "$out" && break
    sleep 0.5
done
echo "waited until $(($(date +%s) - start)) s"
cat "$out"
status=1
if grep -qx 'result: 0 failures' "$out"; then
    status=0
else
    "$adb" logcat -d -s AndroidRuntime:E DEBUG:F libc:F ActivityManager:W | tail -60
fi
again shell am force-stop "$package" || true
again uninstall "$package" > /dev/null || true
rm -f "$out"
exit $status
