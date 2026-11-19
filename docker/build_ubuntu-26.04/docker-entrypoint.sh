#!/bin/sh
# builds server/mp_server from the repo mounted at /code (see docker/build.sh)
[ -z "$CONFIG" ] && config=Release || config="$CONFIG"
cd /code/server || exit 1
if [ "$config" = Debug ]; then make clean >/dev/null 2>&1; make -j"$(nproc)" CXXFLAGS="-O0 -g" || exit 1
else make -j"$(nproc)" || exit 1; fi
if [ "$BUILD_TESTS" = 1 ]; then
    cd /code/test && R=../third_party/omp-raknet && \
    g++ -O1 -std=c++17 -pthread -I../src -I../third_party/mbedtls/include -D__stdcall= -DRAKNET_LEGACY -DPACKETS_PER_TICK=3 \
        -I$R/Include/raknet -I$R sampgate_test.cpp ../src/net/sampgate.cpp ../src/net/client.cpp ../src/net/server.cpp \
        ../src/net/lan.cpp ../src/net/online.cpp ../src/net/authcrypto.cpp ../src/net/samp_import.cpp ../server/obj/*.a -ldl \
        -o sampgate_test || exit 1
    echo "Built test/sampgate_test (run it next to an open.mp server: docker/omp/run_omp.sh)"
fi
[ -n "$HOST_UID" ] && chown -R "$HOST_UID:${HOST_GID:-$HOST_UID}" /code/server/obj /code/server/mp_server /code/test/sampgate_test 2>/dev/null
exit 0
