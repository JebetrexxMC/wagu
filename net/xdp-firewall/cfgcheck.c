/* main() for checking an xdpfw.conf with the loader code; prepend the stubs from xdp-firewall tests/userspace.c (see README). */
#include <stdio.h>
int main(int argc, char **argv)
{
    static config__t cfg;
    if (load_cfg(&cfg, argv[1], 1, NULL) != 0) { printf("LOAD FAILED\n"); return 1; }
    printf("interfaces=%d filters=%d\n", cfg.interfaces_cnt, cfg.filters_cnt);
    for (int i = 0; i < cfg.filters_cnt; i++) print_filter(&cfg.filters[i], i);
    return 0;
}
