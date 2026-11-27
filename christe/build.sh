#!/bin/bash
# Builds ChristeMultiplayer.apk (no Gradle): aapt2 + javac + d8 + zipalign + apksigner, like moquil/build.sh.
#   SDK=~/android-sdk JAVA_HOME=~/jdk KEYSTORE=... KEYPASS=... bash build.sh
# ../bin/libMP32.so (armeabi-v7a, GTA SA 2.00) and ../bin/libMP64.so (arm64-v8a, GTA SA 2.10) go into assets/libs,
# the app copies the right one into date2/libs/<abi>/libMP.so. Optional: OBB=1 also packs
# christe/obb/ into main.<versionCode>.com.christe.mp.obb (the launcher's own resources).
# EDITION=32 builds AML Multiplayer 32-bit (OUT=AMLMultiplayer-32bit.apk): libMP32.so goes into lib/armeabi-v7a/ instead
# of assets, so Android runs the whole app 32-bit on a 64-bit phone (for a GTA SA with no arm64-v8a, GTA SA 2.00).
# The GitHub workflow "Christe Multiplayer" (.github/workflows/christe.yml) runs this.
set -e
cd "$(dirname "$0")"
SDK=${SDK:-${ANDROID_HOME:-$HOME/android-sdk}}; BTV=${BUILD_TOOLS:-34.0.0}; BT=$SDK/build-tools/$BTV; JAR=$SDK/platforms/android-34/android.jar
[ -n "$JAVA_HOME" ] && export PATH=$JAVA_HOME/bin:$PATH
OUT=${OUT:-AMLMultiplayer.apk}
rm -rf build; mkdir -p build/res build/classes build/dex build/assets
cp -r assets/. build/assets/
mkdir -p build/assets/libs
if [ "$EDITION" = "32" ]; then
  [ -f ../bin/libMP32.so ] || { echo "EDITION=32 needs ../bin/libMP32.so"; exit 1; }
  mkdir -p build/lib/armeabi-v7a && cp ../bin/libMP32.so build/lib/armeabi-v7a/ && echo "32-bit edition: lib/armeabi-v7a/libMP32.so"
else
  for l in libMP32.so libMP64.so; do [ -f ../bin/$l ] && cp ../bin/$l build/assets/libs/ && echo "bundled $l"; done
fi
$BT/aapt2 compile --dir res -o build/res/res.zip
$BT/aapt2 link -I $JAR --manifest AndroidManifest.xml -A build/assets -o build/base.apk build/res/res.zip --min-sdk-version 21 --target-sdk-version 28
# stubs/ (com.rockstargames.gtasa.GTASA) only compile against: the real class comes from the installed GTA SA at run time
mkdir -p build/stubs
javac --release 8 -Xlint:-options -cp $JAR -d build/stubs $(find stubs -name '*.java')
javac --release 8 -Xlint:-options -cp $JAR:build/stubs -d build/classes $(find src -name '*.java')
$BT/d8 --min-api 21 --lib $JAR --classpath build/stubs --output build/dex $(find build/classes -name '*.class')
cp build/base.apk build/unsigned.apk
(cd build/dex && zip -q ../unsigned.apk classes.dex)
[ -d build/lib ] && (cd build && zip -qr unsigned.apk lib)
$BT/zipalign -f -p 4 build/unsigned.apk build/aligned.apk
$BT/apksigner sign --ks "$KEYSTORE" --ks-pass env:KEYPASS --out "$OUT" build/aligned.apk
$BT/apksigner verify "$OUT" && ls -la "$OUT"
if [ "$OBB" = "1" ] && [ -d obb ]; then
  VC=$(grep -o 'versionCode="[0-9]*"' AndroidManifest.xml | grep -o '[0-9]*')
  (cd obb && zip -qr ../main.$VC.com.christe.mp.obb .) && ls -la main.$VC.com.christe.mp.obb
fi
