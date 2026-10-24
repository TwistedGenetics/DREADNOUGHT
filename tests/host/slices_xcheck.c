/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT host test helper: the firmware's loop keys and AUTO slices of the built-in loops, as JSON, for
 * tests/host/slices_xcheck.mjs to check the breaks page's JavaScript (web/breaks/slices.js) against. */
#include <stdio.h>
#include "dn_breaks.h"
#include "dn_breaks_builtin.h"
static dn_brk_t B;
int main(void)
{
    printf("[");
    for (unsigned k = 0; k < DN_BRK_BUILTIN; k++) {
        dn_loop_t L = {DN_BRK_BUILTIN_LOOPS[k].adpcm, DN_BRK_BUILTIN_LOOPS[k].n, DN_BRK_BUILTIN_LOOPS[k].rate, 2, ""};
        dn_brk_init(&B);
        dn_brk_load(&B, &L);
        printf("%s{\"n\":%u,\"key\":%u,\"auto\":[", k ? "," : "", L.n, B.key);
        for (unsigned i = 0; i < B.n_auto; i++)
            printf("%s%u", i ? "," : "", B.auto_start[i]);
        printf("],\"adpcm\":\"");
        for (unsigned i = 0; i < (L.n + 1) / 2; i++)
            printf("%02x", L.adpcm[i]);
        printf("\"}");
    }
    printf("]\n");
    return 0;
}
