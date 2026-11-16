#!/bin/bash
# 4.13.63: builds ViceLoader.apk (VICE Loader for GTA Vice City) without Gradle.
#   source ~/env.sh   (JAVA_HOME, NDK_HOME, ANDROID_HOME)
#   bash viceloader/build_apk.sh            -> viceloader/ViceLoader.apk
# Steps: CMake (NDK) for armeabi-v7a + arm64-v8a -> assets/bundle (AML, libMP with MonetLoader built in, moonloader/lib)
#        -> aapt2 -> javac -> d8 -> zip lib/ -> zipalign -> apksigner.
# KEYSTORE/KEYPASS sign with your key; otherwise a local debug key (build/debug.keystore) is made.
set -e
cd "$(dirname "$0")"
ROOT=$(cd .. && pwd)
SDK=${ANDROID_HOME:-${SDK:-$HOME/android-sdk}}; BT=$SDK/build-tools/34.0.0; JAR=$SDK/platforms/android-34/android.jar
NDK=${NDK_HOME:-$ANDROID_NDK_HOME}
export PATH=${JAVA_HOME:-$HOME/jdk}/bin:$PATH
STRIP=$(ls $NDK/toolchains/llvm/prebuilt/*/bin/llvm-strip | head -1)
ABIS=${ABIS:-"armeabi-v7a arm64-v8a"}
rm -rf build/res build/classes build/dex build/apk build/assets build/gen build/*.apk; mkdir -p build/gen build/res build/classes build/dex build/apk/lib build/assets/bundle

# native: libviceloader.so (ImGui + MP launcher UI + VICE City LAN server)
for A in $ABIS; do
  cmake -S jni -B build/cmake-$A -G Ninja -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
        -DANDROID_ABI=$A -DANDROID_PLATFORM=android-24 -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 >/dev/null
  ninja -C build/cmake-$A
  mkdir -p build/apk/lib/$A
  $STRIP -o build/apk/lib/$A/libviceloader.so build/cmake-$A/libviceloader.so
done

# assets: UI background + the bundle exported to Download/ViceLoader by the app
B=build/assets/bundle
cp $ROOT/assets/MP/ui_bg.jpg build/assets/ui_bg.jpg
cp $ROOT/assets/MP/chat_icon.png build/assets/chat_icon.png     # floating bar chat pill (same as San Andreas)
cp assets/global_click.wav build/assets/global_click.wav        # global_click like the San Andreas MP UI
for A in $ABIS; do
  mkdir -p $B/AML/$A
  cp prebuilt/$A/libAML.so $B/AML/$A/
done
mkdir -p $B/mods
cp $ROOT/bin/libMP32.so $ROOT/bin/libMP64.so $B/mods/
mkdir -p $B/moonloader && cp -r $ROOT/assets/MP/moonloader/lib $B/moonloader/lib
cp bundle_README.txt $B/README.txt

$BT/aapt2 compile --dir res -o build/res/res.zip
$BT/aapt2 link -I $JAR --manifest AndroidManifest.xml -o build/base.apk -A build/assets build/res/res.zip \
     --min-sdk-version 24 --target-sdk-version 34 -0 so -0 jpg -0 wav --java build/gen
javac --release 8 -Xlint:-options -cp $JAR -d build/classes $(find src build/gen -name '*.java')
# build-tools 34 d8 (R8 8.2.2) crashes on JDK 21 anonymous classes -> newer D8 from Google Maven
R8_JAR=${R8_JAR:-$HOME/.cache/r8-8.5.35.jar}
[ -f "$R8_JAR" ] || { mkdir -p "$(dirname "$R8_JAR")"; curl -sfL -o "$R8_JAR" https://dl.google.com/android/maven2/com/android/tools/r8/8.5.35/r8-8.5.35.jar; }
java -cp "$R8_JAR" com.android.tools.r8.D8 --min-api 24 --lib $JAR --output build/dex $(find build/classes -name '*.class')
cp build/base.apk build/unsigned.apk
(cd build/dex && zip -q ../unsigned.apk classes.dex)
(cd build/apk && zip -q -0 -r ../unsigned.apk lib)   # extractNativeLibs=false: stored + page aligned
$BT/zipalign -f -p 4 build/unsigned.apk build/aligned.apk
if [ -z "$KEYSTORE" ]; then
  KEYSTORE=build/debug.keystore; export KEYPASS=android
  [ -f $KEYSTORE ] || keytool -genkeypair -keystore $KEYSTORE -storepass android -keypass android -alias viceloader \
          -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=VICE Loader, O=ThirtyFoxMCx00E1" >/dev/null 2>&1
fi
$BT/apksigner sign --ks "$KEYSTORE" --ks-pass env:KEYPASS --out ViceLoader.apk build/aligned.apk
$BT/apksigner verify ViceLoader.apk && ls -la ViceLoader.apk
