// MP 5.02.70: MP LAN server beacon over PJON LocalUDP (https://github.com/JebetrexxMC/PJON).
// A box that hosts an MP LAN server sends "MPLAN <port> <players>/<max> <name>" to every PJON device on the
// subnet once a second; Arduino / ESP32 boards on the same bus (a LAN-party display, a status LED) and other
// Linux boxes can show which servers are up without MQTT or a broker.
//   beacon:  ./mp-pjon-beacon send 7777 "MP LAN Server" 3 50
//   listen:  ./mp-pjon-beacon listen
// PJON LocalUDP broadcasts on UDP port 7100 (allow it in the firewall). Build: make (PJON=/path/to/PJON).
#include <PJONLocalUDP.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void on_packet(uint8_t* payload, uint16_t length, const PJON_Packet_Info& info)
{
    if(length < 6 || memcmp(payload, "MPLAN ", 6) != 0) return;   // other PJON traffic on the bus
    printf("device %u: %.*s\n", (unsigned)info.tx.id, (int)length, (const char*)payload);
    fflush(stdout);
}

int main(int argc, char** argv)
{
    const bool send = argc >= 3 && !strcmp(argv[1], "send");
    if(!send && !(argc >= 2 && !strcmp(argv[1], "listen")))
    {
        fprintf(stderr, "usage: %s send <port> <name> [players] [max] | listen\n", argv[0]);
        return 2;
    }
    PJONLocalUDP bus(send ? 77 : 78);   // device ids: 77 beacon, 78 listener
    bus.set_receiver(on_packet);
    bus.begin();
    char msg[200];
    if(send)
        snprintf(msg, sizeof(msg), "MPLAN %d %d/%d %s", atoi(argv[2]), argc > 4 ? atoi(argv[4]) : 0,
                 argc > 5 ? atoi(argv[5]) : 50, argc > 3 ? argv[3] : "MP LAN Server");
    uint32_t last = 0;
    for(;;)
    {
        bus.receive(1000);
        bus.update();
        if(send && (uint32_t)(PJON_MILLIS() - last) > 1000)
        {
            last = PJON_MILLIS();
            bus.send_packet(PJON_BROADCAST, msg, (uint16_t)strlen(msg));   // broadcast: no ACK
        }
    }
}
