/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: src/dsp/dn_math.h against libm. Copyright (C) 2026 Jamie (TwistedGenetics). */
#include <math.h>
#include <stdio.h>
#include "dn_math.h"
static int fails;
static void bound(const char *what, double err, double lim)
{
    printf("%s %-34s %.3g (limit %.3g)\n", err <= lim ? "  ok  " : "  FAIL", what, err, lim);
    fails += err > lim;
}
int main(void)
{
    double e1 = 0, e2 = 0, e3 = 0, e4 = 0, e5 = 0, e6 = 0, e7 = 0, e8 = 0, e9 = 0, e10 = 0;
    int i;
    for (i = 0; i <= 2000000; i++) {
        float t = (float)i / 2000000.f, x;
        x = -126.f + 253.f * t;                         /* exp2 */
        { double r = exp2((double)x), g = fm_exp2f(x); if (r > 1e-37) e1 = fmax(e1, fabs(g - r) / r); }
        x = -87.f + 175.f * t;
        { double r = exp((double)x), g = fm_expf(x); if (r > 1e-37) e2 = fmax(e2, fabs(g - r) / r); }
        x = 0.25f + 3.75f * t;
        e3 = fmax(e3, fabs(fm_log2f(x) - log2((double)x)));
        x = powf(10.f, -30.f + 60.f * t);
        e4 = fmax(e4, fabs(fm_log2f(x) - log2((double)x)) / fmax(1.0, fabs(log2((double)x))));
        x = -8.f + 16.f * t;
        e5 = fmax(e5, fabs(fm_tanhf(x) - tanh((double)x)));
        x = -100.f + 200.f * t;
        e6 = fmax(e6, fabs(fm_sinf(x) - sin((double)x)));
        e7 = fmax(e7, fabs(fm_cosf(x) - cos((double)x)));
        x = (-1.5707f + 3.1414f * t);
        { double r = tan((double)x); if (fabs(r) > 1e-3) e8 = fmax(e8, fabs(fm_tanf(x) - r) / fabs(r)); }
        x = 1e-6f + 1e6f * t;
        e9 = fmax(e9, fabs(fm_sqrtf(x) - sqrt((double)x)) / sqrt((double)x));
        x = 0.01f + 100.f * t;
        { double r = pow((double)x, 1.37), g = fm_powf(x, 1.37f); e10 = fmax(e10, fabs(g - r) / r); }
    }
    bound("exp2 rel, [-126, 127]", e1, 3e-7);
    bound("exp rel, [-87, 88]", e2, 4e-7);
    bound("log2 abs, [0.25, 4]", e3, 3e-7);
    bound("log2 rel, [1e-30, 1e30]", e4, 2e-7);
    bound("tanh abs", e5, 3e-7);
    bound("sin abs, [-100, 100]", e6, 1e-6);
    bound("cos abs, [-100, 100]", e7, 1e-6);
    bound("tan rel, (-pi/2, pi/2)", e8, 2e-6);
    bound("sqrt rel", e9, 2e-7);
    bound("pow rel, x^1.37 on [0.01, 100]", e10, 2e-6);
    {
        float f = 0.0f;
        for (i = -300; i <= 300; i++)
            f += fm_floorf((float)i * 0.37f) == floorf((float)i * 0.37f) ? 0.f : 1.f;
        bound("floor wrong", f, 0);
    }
    printf(fails ? "MATH TEST FAILED\n" : "MATH TEST: ok\n");
    return fails != 0;
}
