# MP NetWire: make          -> build/mpnetd (host)
#             make android  -> build/android/<abi>/mpnetd (Termux / adb shell), needs NDK=/path/to/ndk
#             make test
#             make dns      -> build/mpnet-dns (libevent evdns bulk DNS check, needs libevent-dev)
#             make ws       -> build/mpnet-ws  (POCO WebSocket live status push, needs libpoco-dev)
CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Wno-unused-parameter
SRC = lib/mpnet.cpp server/mpnetd.cpp
NDK ?= $(ANDROID_NDK_LATEST_HOME)
TC = $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin

build/mpnetd: $(SRC) lib/mpnet.h ../src/net/protocol.h ../src/net/dz_build.h ../src/net/cgrp.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lpthread

android: $(SRC)
	@mkdir -p build/android/arm64-v8a build/android/armeabi-v7a
	$(TC)/aarch64-linux-android24-clang++ -O2 -std=c++17 -static-libstdc++ -s -o build/android/arm64-v8a/mpnetd $(SRC)
	$(TC)/armv7a-linux-androideabi24-clang++ -O2 -std=c++17 -static-libstdc++ -s -o build/android/armeabi-v7a/mpnetd $(SRC)

test: build/mpnetd
	bash tests/run.sh

dns: server/mpnet-dns.c
	@mkdir -p build
	$(CC) -O2 -Wall -o build/mpnet-dns server/mpnet-dns.c -levent

ws: server/mpnet-ws.cpp
	@mkdir -p build
	$(CXX) -O2 -std=c++17 -o build/mpnet-ws server/mpnet-ws.cpp -lPocoNet -lPocoUtil -lPocoJSON -lPocoFoundation -lpthread

clean:
	rm -rf build
.PHONY: android test clean dns ws
