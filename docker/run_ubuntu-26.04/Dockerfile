FROM ubuntu:26.04
ENV DEBIAN_FRONTEND=noninteractive
RUN \
    apt-get update && \
    apt-get install -y --no-install-recommends \
        ca-certificates \
        procps \
        tzdata \
    && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /data
COPY mp /opt/mp
ENV MP_PORT=1922 MP_NAME="MP LAN Server" MP_MAX=100 SAMP_PORT=7777 SAMP_MAX=50 REGION=Asia
# SAMP_PORT=0: no PC crossplay
CMD ["/bin/sh", "-c", "cd /data 2>/dev/null || cd ~; if [ \"$SAMP_PORT\" != 0 ]; then exec /opt/mp/mp_server \"$MP_PORT\" \"$MP_NAME\" \"$MP_MAX\" \"$REGION\" --persistent --samp-port \"$SAMP_PORT\" --samp-max \"$SAMP_MAX\"; else exec /opt/mp/mp_server \"$MP_PORT\" \"$MP_NAME\" \"$MP_MAX\" \"$REGION\" --persistent; fi"]
