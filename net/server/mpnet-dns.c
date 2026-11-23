/* mpnet-dns: resolve every host in net/wires.txt at once with libevent's async DNS (evdns)
 * (fork: github.com/JebetrexxMC/libevent). Prints JSON: host, IPv4 list, time in ms, error.
 * A wire whose name does not resolve is "down" before any ping is sent, so this is the first
 * thing to check when the status page goes red (duckdns / playit names expire or move).
 *
 *   cc -O2 -o build/mpnet-dns server/mpnet-dns.c -levent      (make dns)
 *   build/mpnet-dns wires.txt [more.txt ...]   ->  JSON on stdout
 */
#include <event2/event.h>
#include <event2/dns.h>
#include <event2/util.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define MAXH 512
struct job { char host[256]; double t0, ms; char ips[256]; const char *err; int done; };
static struct job jobs[MAXH];
static int njobs, pending;
static struct event_base *base;

static double now_ms(void) { struct timeval tv; gettimeofday(&tv, NULL); return tv.tv_sec * 1e3 + tv.tv_usec / 1e3; }

static void on_dns(int err, struct evutil_addrinfo *ai, void *arg) {
    struct job *j = arg;
    j->ms = now_ms() - j->t0;
    j->done = 1;
    if (err) j->err = evutil_gai_strerror(err);
    for (struct evutil_addrinfo *a = ai; a; a = a->ai_next) {
        char b[64];
        if (a->ai_family != AF_INET) continue;
        inet_ntop(AF_INET, &((struct sockaddr_in *)a->ai_addr)->sin_addr, b, sizeof b);
        if (strstr(j->ips, b)) continue;
        if (strlen(j->ips) + strlen(b) + 4 < sizeof j->ips) {
            if (j->ips[0]) strcat(j->ips, "\",\"");
            strcat(j->ips, b);
        }
    }
    if (ai) evutil_freeaddrinfo(ai);
    if (--pending == 0) event_base_loopexit(base, NULL);
}

static void add_host(const char *h) {
    if (!*h || njobs >= MAXH) return;
    for (int i = 0; i < njobs; i++) if (!strcmp(jobs[i].host, h)) return;
    snprintf(jobs[njobs++].host, sizeof jobs[0].host, "%s", h);
}

/* wires.txt line: id | group | kind | host:port | name | opts */
static void read_wires(const char *path) {
    FILE *f = fopen(path, "r");
    char line[1024];
    if (!f) { fprintf(stderr, "mpnet-dns: cannot open %s\n", path); return; }
    while (fgets(line, sizeof line, f)) {
        char *p = line, *field[4] = {0};
        int n = 0;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || !*p) continue;
        for (char *s = strtok(p, "|"); s && n < 4; s = strtok(NULL, "|")) field[n++] = s;
        if (n < 4) continue;
        char *h = field[3];
        while (*h == ' ') h++;
        char *c = strrchr(h, ':');
        if (c) *c = 0;
        for (char *e = h + strlen(h); e > h && (e[-1] == ' ' || e[-1] == '\n'); ) *--e = 0;
        if (*h == '[' || strchr(h, '<')) continue; /* templates like <your-zt-ip> */
        add_host(h);
    }
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: mpnet-dns wires.txt [more.txt ...]\n"); return 2; }
    for (int i = 1; i < argc; i++) read_wires(argv[i]);
    base = event_base_new();
    struct evdns_base *dns = evdns_base_new(base, EVDNS_BASE_INITIALIZE_NAMESERVERS);
    if (!base || !dns) { fprintf(stderr, "mpnet-dns: libevent init failed\n"); return 1; }
    evdns_base_set_option(dns, "timeout:", "3");
    evdns_base_set_option(dns, "attempts:", "2");
    struct evutil_addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    pending = njobs;
    for (int i = 0; i < njobs; i++) {
        jobs[i].t0 = now_ms();
        evdns_getaddrinfo(dns, jobs[i].host, NULL, &hints, on_dns, &jobs[i]); /* may call back at once */
    }
    if (pending > 0) event_base_dispatch(base);
    int ok = 0;
    printf("{\"hosts\":[\n");
    for (int i = 0; i < njobs; i++) {
        struct job *j = &jobs[i];
        int good = j->done && !j->err && j->ips[0];
        ok += good;
        printf("  {\"host\":\"%s\",\"ok\":%s,\"ms\":%.1f,\"ips\":[%s%s%s]%s%s%s}%s\n", j->host, good ? "true" : "false", j->ms,
               j->ips[0] ? "\"" : "", j->ips, j->ips[0] ? "\"" : "",
               good ? "" : ",\"error\":\"", good ? "" : (j->err ? j->err : "no IPv4 address"), good ? "" : "\"",
               i + 1 < njobs ? "," : "");
    }
    printf("],\"total\":%d,\"resolved\":%d}\n", njobs, ok);
    evdns_base_free(dns, 0);
    event_base_free(base);
    return 0;
}
