# mpnetd (NetWire relay / probe / watch) as a small container: docker build -f net/k8s/Dockerfile .
FROM debian:stable-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ make && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY net/ net/
COPY src/net/ src/net/
RUN make -C net build/mpnetd

FROM debian:stable-slim
COPY --from=build /src/net/build/mpnetd /usr/local/bin/mpnetd
COPY net/wires.txt /etc/mpnet/wires.txt
EXPOSE 8090/tcp 7777/udp
USER 65534:65534
ENTRYPOINT ["/usr/local/bin/mpnetd"]
# default: the 24/7 status watcher; relay: args ["relay","-l","7777","-u","host:port","--kind","samp","--http","8091"]
CMD ["watch", "-w", "/etc/mpnet/wires.txt", "-o", "/tmp/netwire", "-i", "60", "--http", "8090"]
