#!/bin/sh
# Runs a test application in the Android emulator (record mui-0008), as
# Maul Window's runner does: installs it on the device ANDROID_SERIAL
# names (else the only one), starts its activity, and waits, a minute at
# most, for the closing line it writes to files/out, "result: N
# failures", read with run-as (the application is debuggable). The
# application never ends itself, so the runner stops it. Passes when the
# line says 0 failures; otherwise shows the application's crashes from
# the log.
set -eu
adb=${ADB:-adb}
apk=$1
package=$2
start=$(date +%s)
"$adb" install -r "$apk" > /dev/null
"$adb" logcat -c
"$adb" shell am start -W -n "$package/maul.ui.tests.TestActivity" > /dev/null
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
"$adb" shell am force-stop "$package"
"$adb" uninstall "$package" > /dev/null
rm -f "$out"
exit $status
