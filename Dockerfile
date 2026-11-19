# 3.38: the 4 MP servers with their 24/7 bot host ("Broker") in one container.
# Railway / Render / Fly / Koyeb / any Docker host: deploy this repo, no ports needed (the game goes
# through the MQTT lobby). Variable REGION = Asia (default) | Europe | NA | Oceania.
# 5.02: more images (Ubuntu 26.04 / Debian 12, open.mp, the Ubuntu ISO): docker/README.md
FROM debian:12-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ make && rm -rf /var/lib/apt/lists/*
COPY src/net /src/src/net
COPY third_party/mbedtls /src/third_party/mbedtls
COPY third_party/pawn-amx /src/third_party/pawn-amx
COPY third_party/sqlite /src/third_party/sqlite
COPY third_party/puff /src/third_party/puff
COPY third_party/unrar /src/third_party/unrar
COPY third_party/omp-raknet /src/third_party/omp-raknet
COPY server/Makefile server/mp_server.cpp /src/server/
RUN make -C /src/server -j"$(nproc)"

FROM debian:12-slim
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates procps && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/server/mp_server /mp/mp_server
COPY server/run_24_7.sh /mp/run_24_7.sh
ENV REGION=Asia FOREGROUND=1
WORKDIR /mp
CMD ["/mp/run_24_7.sh"]
