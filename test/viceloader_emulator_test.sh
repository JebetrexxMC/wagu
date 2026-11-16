#!/bin/bash
# 4.13.64: VICE Loader overlay test on an Android emulator (run by .github/workflows/viceloader-test.yml).
# Installs ViceLoader.apk, grants "Display over other apps", opens the floating bar, the dropdown, every
# launcher page and the chat keyboard, takes a screenshot of each, checks the overlay process stays alive,
# then checks the crash log (Java + native crash tests -> Download/ViceLoader/crash_*.txt).
# Output: out/ (screenshots, logcat, crash logs, summary.txt). Exit 1 if the overlay crashed during the UI test.
APK=${1:-ViceLoader.apk}
PKG=com.thirtyfoxmc.viceloader
OUT=out/${2:-test}; mkdir -p $OUT/shots $OUT/logs
fails=0
log() { echo "$*" | tee -a $OUT/summary.txt; }
alive() { local p; p=$(adb shell pidof $PKG 2>/dev/null | tr -d '\r'); [ -n "$p" ] && [ "$p" = "$PID0" ]; }   # same process = no crash + restart
shot() { adb exec-out screencap -p > "$OUT/shots/$1.png" 2>/dev/null; }
open_() { adb shell am start -n $PKG/.MainActivity --es open "$1" > /dev/null 2>&1; sleep "${2:-4}"; }

adb wait-for-device
log "device: $(adb shell getprop ro.product.model) Android $(adb shell getprop ro.build.version.release) abi=$(adb shell getprop ro.product.cpu.abilist) bridge=$(adb shell getprop ro.dalvik.vm.native.bridge)"
adb shell settings put global window_animation_scale 0; adb shell settings put global transition_animation_scale 0; adb shell settings put global animator_duration_scale 0
adb shell pm uninstall $PKG > /dev/null 2>&1   # a clean install for every run (no saved prefs / crash files)
adb shell rm -rf /sdcard/Download/ViceLoader
log "install $(basename "$APK"): $(adb install -r -g "$APK" 2>&1 | tail -1)"
adb shell appops set $PKG SYSTEM_ALERT_WINDOW allow
adb logcat -c
adb shell am start -n $PKG/.MainActivity > /dev/null; sleep 5; shot 00_start_screen
PID0=$(adb shell pidof $PKG | tr -d '\r'); log "process $PID0"

# UI test: bar, launcher, every page, dropdown, chat keyboard
steps="bar:01_floating_bar panel:02_launcher_internet page:1:03_featured page:2:04_favorites page:4:05_add_server page:5:06_chat page:6:07_players page:7:08_voice page:8:09_monetloader page:9:10_help page:10:11_mods page:11:12_settings page:12:13_about menu:14_dropdown chat:15_chat_keyboard bar:16_back_to_bar"
for s in $steps; do
  name=${s##*:}; what=${s%:*}
  open_ "$what" 5
  shot "$name"
  if alive; then log "ok    $what ($name)"; else log "CRASH $what ($name): overlay process died"; fails=$((fails+1)); adb shell am start -n $PKG/.MainActivity > /dev/null; sleep 5; PID0=$(adb shell pidof $PKG | tr -d '\r'); fi
done
# touch test: drag the bar and tap the middle of the screen with the launcher open
open_ panel 3; adb shell input tap 540 1200; sleep 1; adb shell input swipe 300 1200 300 600 300; sleep 2; shot 17_touch_scroll
alive && log "ok    touch / scroll in the launcher" || { log "CRASH touch / scroll"; fails=$((fails+1)); PID0=$(adb shell pidof $PKG | tr -d '\r'); }
open_ bar 3
open_ savelog 4
adb logcat -d -v threadtime > $OUT/logs/logcat_ui_test.txt
grep -E "FATAL EXCEPTION|Fatal signal|AndroidRuntime: |DEBUG   :|libviceloader" $OUT/logs/logcat_ui_test.txt > $OUT/logs/crash_lines_ui_test.txt
[ -s $OUT/logs/crash_lines_ui_test.txt ] && { log "crash lines in logcat during the UI test:"; head -40 $OUT/logs/crash_lines_ui_test.txt | tee -a $OUT/summary.txt; fails=$((fails+1)); } || log "ok    no FATAL EXCEPTION / Fatal signal in logcat during the UI test"

# crash log test (deliberate crashes, after the UI test)
adb logcat -c
for kind in java native; do
  adb shell am start -n $PKG/.MainActivity --es crashtest $kind > /dev/null; sleep 6
  alive && adb shell am force-stop $PKG
  adb shell am start -n $PKG/.MainActivity > /dev/null; sleep 6   # exportPending -> Download/ViceLoader/crash_*.txt
  shot "20_after_${kind}_crash"
done
adb logcat -d -v threadtime > $OUT/logs/logcat_crash_test.txt
adb shell ls -l /sdcard/Download/ViceLoader/ 2>&1 | tail -n +1 > /dev/null; adb shell ls -l /sdcard/Download/ViceLoader/ 2>&1 | tee -a $OUT/summary.txt
for f in $(adb shell ls /sdcard/Download/ViceLoader/ 2>/dev/null | tr -d '\r' | grep '\.txt$'); do adb pull "/sdcard/Download/ViceLoader/$f" $OUT/logs/ > /dev/null 2>&1; done
n=$(ls $OUT/logs/crash_*.txt 2>/dev/null | wc -l)
grep -l "Java crash" $OUT/logs/crash_*.txt > /dev/null 2>&1 && log "ok    Java crash log saved" || log "MISSING Java crash log"
grep -l "native crash" $OUT/logs/crash_*.txt > /dev/null 2>&1 && log "ok    native crash log saved" || log "MISSING native crash log"
log "crash log files: $n"
log "UI test failures: $fails"
[ $fails -eq 0 ]
