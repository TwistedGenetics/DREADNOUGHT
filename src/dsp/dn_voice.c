/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: the shared voice pool (see dn_voice.h). Copyright (C) 2026 Jamie (TwistedGenetics). */
#include "dn_voice.h"

void dn_vp_init(dn_vpool_t *p, uint32_t n)
{
    uint32_t i;
    p->n = n > DN_VP_MAX ? DN_VP_MAX : n;
    p->clock = 0;
    for (i = 0; i < DN_VP_MAX; i++) {
        p->v[i].owner = DN_VP_NONE;
        p->v[i].note = 0;
        p->v[i].held = 0;
        p->v[i].age = 0;
    }
    for (i = 0; i < 8u; i++)
        p->limit[i] = (uint8_t)p->n;
}

uint32_t dn_vp_count(const dn_vpool_t *p, uint32_t owner)
{
    uint32_t i, c = 0;
    for (i = 0; i < p->n; i++)
        c += p->v[i].owner == owner;
    return c;
}

/* the oldest voice of owner (any owner: DN_VP_NONE) that is held / released (DN_VP_NONE: there's none) */
static uint32_t dn_vp_oldest(const dn_vpool_t *p, uint32_t owner, uint32_t held)
{
    uint32_t i, best = DN_VP_NONE, age = 0;
    for (i = 0; i < p->n; i++) {
        const dn_voice_t *v = &p->v[i];
        if (v->owner == DN_VP_NONE || (owner != DN_VP_NONE && v->owner != owner) || v->held != held)
            continue;
        if (best == DN_VP_NONE || p->clock - v->age > age)
            best = i, age = p->clock - v->age;
    }
    return best;
}

uint32_t dn_vp_alloc(dn_vpool_t *p, uint32_t owner, uint32_t note, int *stolen)
{
    uint32_t i = DN_VP_NONE, k;
    *stolen = 0;
    if (!p->n)
        return DN_VP_NONE;
    if (dn_vp_count(p, owner) >= (owner < 8u ? p->limit[owner] : p->n)) {   /* at its limit: one of its own */
        i = dn_vp_oldest(p, owner, 0);
        if (i == DN_VP_NONE)
            i = dn_vp_oldest(p, owner, 1);
    }
    if (i == DN_VP_NONE)
        for (k = 0; k < p->n; k++)
            if (p->v[k].owner == DN_VP_NONE) {
                i = k;
                break;
            }
    if (i == DN_VP_NONE)
        i = dn_vp_oldest(p, DN_VP_NONE, 0);
    if (i == DN_VP_NONE)
        i = dn_vp_oldest(p, DN_VP_NONE, 1);
    if (i == DN_VP_NONE)
        return DN_VP_NONE;
    *stolen = p->v[i].owner != DN_VP_NONE;
    p->v[i].owner = (uint8_t)owner;
    p->v[i].note = (uint8_t)note;
    p->v[i].held = 1;
    p->v[i].age = ++p->clock;
    return i;
}

uint32_t dn_vp_release(dn_vpool_t *p, uint32_t owner, uint32_t note)
{
    uint32_t i, best = DN_VP_NONE;
    for (i = 0; i < p->n; i++)                      /* the newest held voice of that note */
        if (p->v[i].owner == owner && p->v[i].note == note && p->v[i].held &&
            (best == DN_VP_NONE || p->v[i].age > p->v[best].age))
            best = i;
    if (best != DN_VP_NONE)
        p->v[best].held = 0;
    return best;
}

void dn_vp_free(dn_vpool_t *p, uint32_t i)
{
    if (i < p->n)
        p->v[i].owner = DN_VP_NONE, p->v[i].held = 0;
}
