/* SPDX-License-Identifier: GPL-3.0-only
 * The plugin's core behind a pipe, for tests/host/plugin_editor.mjs: the web editor's own code (web/editor) talks to
 * it as it talks to the plugin's MIDI port. Frames (F0 .. F7) in on stdin, the answers out on stdout; the core runs
 * a few milliseconds before each frame, as the plugin's audio thread runs it between frames. Copyright (C) 2026 Jamie
 * (TwistedGenetics). */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "dn_core.h"

int main(void)
{
    static uint8_t in[4096], f[8192], out[2048];
    static float audio[2 * 128];
    uint32_t fn = 0, i, k;
    ssize_t n;
    dnc_init();
    for (k = 0; k < 600u; k++)
        dnc_render(audio, 128);                     /* (past the splash) */
    while ((n = read(0, in, sizeof in)) > 0) {
        for (i = 0; i < (uint32_t)n; i++) {
            if (in[i] == 0xF0u)
                fn = 0;
            if (fn < sizeof f)
                f[fn++] = in[i];
            if (in[i] != 0xF7u)
                continue;
            for (k = 0; k < 2u; k++)
                dnc_render(audio, 128);
            dnc_sysex(f, fn);
            fn = 0;
            {
                uint32_t m;
                while ((m = dnc_sysex_out(out, sizeof out)) > 0)
                    fwrite(out, 1, m, stdout);
                fflush(stdout);
            }
        }
    }
    return 0;
}
