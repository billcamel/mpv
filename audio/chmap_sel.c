/*
 * This file is part of mpv.
 *
 * mpv is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mpv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with mpv.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <assert.h>
#include <limits.h>

#include "common/common.h"
#include "chmap_sel.h"

static const struct mp_chmap speaker_replacements[][2] = {
    // 5.1 <-> 5.1 (side)
    { MP_CHMAP2(SL, SR), MP_CHMAP2(BL, BR) },
    // 7.1 <-> 7.1 (rear ext)
    { MP_CHMAP2(SL, SR), MP_CHMAP2(SDL, SDR) },
};

// Try to replace speakers from the left of the list with the ones on the
// right, or the other way around.
static bool replace_speakers(struct mp_chmap *map, struct mp_chmap list[2])
{
    assert(list[0].num == list[1].num);
    if (!mp_chmap_is_valid(map))
        return false;
    for (int dir = 0; dir < 2; dir++) {
        int from = dir ? 0 : 1;
        int to   = dir ? 1 : 0;
        bool replaced = false;
        struct mp_chmap t = *map;
        for (int n = 0; n < t.num; n++) {
            for (int i = 0; i < list[0].num; i++) {
                if (t.speaker[n] == list[from].speaker[i]) {
                    t.speaker[n] = list[to].speaker[i];
                    replaced = true;
                    break;
                }
            }
        }
        if (replaced && mp_chmap_is_valid(&t)) {
            *map = t;
            return true;
        }
    }
    return false;
}

// Allow all channel layouts that can be expressed with mp_chmap.
// (By default, all layouts are rejected.)
void mp_chmap_sel_add_any(struct mp_chmap_sel *s)
{
    s->allow_any = true;
}

// Allow all waveext formats, and force waveext channel order.
void mp_chmap_sel_add_waveext(struct mp_chmap_sel *s)
{
    s->allow_waveext = true;
}

// Classic ALSA-based MPlayer layouts.
void mp_chmap_sel_add_alsa_def(struct mp_chmap_sel *s)
{
    for (int n = 0; n < MP_NUM_CHANNELS; n++) {
        struct mp_chmap t;
        mp_chmap_from_channels_alsa(&t, n);
        if (t.num)
            mp_chmap_sel_add_map(s, &t);
    }
}

// Add a channel map that should be allowed.
void mp_chmap_sel_add_map(struct mp_chmap_sel *s, const struct mp_chmap *map)
{
    if (!mp_chmap_is_valid(map))
        return;
    if (!s->chmaps)
        s->chmaps = s->chmaps_storage;
    if (s->num_chmaps == MP_ARRAY_SIZE(s->chmaps_storage)) {
        if (!s->tmp)
            return;
        s->chmaps = talloc_memdup(s->tmp, s->chmaps, sizeof(s->chmaps_storage));
    }
    if (s->chmaps != s->chmaps_storage)
        MP_TARRAY_GROW(s->tmp, s->chmaps, s->num_chmaps);
    s->chmaps[s->num_chmaps++] = *map;
}

// Allow all waveext formats in default order.
void mp_chmap_sel_add_waveext_def(struct mp_chmap_sel *s)
{
    for (int n = 1; n < MP_NUM_CHANNELS; n++) {
        struct mp_chmap map;
        mp_chmap_from_channels(&map, n);
        mp_chmap_sel_add_map(s, &map);
    }
}

// Whitelist a speaker (MP_SPEAKER_ID_...). All layouts that contain whitelisted
// speakers are allowed.
void mp_chmap_sel_add_speaker(struct mp_chmap_sel *s, int id)
{
    assert(id >= 0 && id < MP_SPEAKER_ID_COUNT);
    s->speakers[id] = true;
}

static bool test_speakers(const struct mp_chmap_sel *s, struct mp_chmap *map)
{
    for (int n = 0; n < map->num; n++) {
        if (!s->speakers[map->speaker[n]])
            return false;
    }
    return true;
}

static bool test_maps(const struct mp_chmap_sel *s, struct mp_chmap *map)
{
    for (int n = 0; n < s->num_chmaps; n++) {
        if (mp_chmap_equals_reordered(&s->chmaps[n], map)) {
            *map = s->chmaps[n];
            return true;
        }
    }
    return false;
}

static bool test_waveext(const struct mp_chmap_sel *s, struct mp_chmap *map)
{
    if (s->allow_waveext) {
        struct mp_chmap t = *map;
        mp_chmap_reorder_to_waveext(&t);
        if (mp_chmap_is_waveext(&t)) {
            *map = t;
            return true;
        }
    }
    return false;
}

static bool test_layout(const struct mp_chmap_sel *s, struct mp_chmap *map)
{
    if (!mp_chmap_is_valid(map))
        return false;

    return s->allow_any || test_waveext(s, map) || test_speakers(s, map) ||
           test_maps(s, map);
}

// Determine which channel map to use given a source channel map, and various
// parameters restricting possible choices. If the map doesn't match, select
// a fallback and set it.
// If no matching layout is found, a reordered layout may be returned.
// If that is not possible, a fallback for up/downmixing may be returned.
// If no choice is possible, set *map to empty.
bool mp_chmap_sel_adjust(const struct mp_chmap_sel *s, struct mp_chmap *map)
{
    if (test_layout(s, map))
        return true;
    if (mp_chmap_is_unknown(map)) {
        struct mp_chmap t = {0};
        if (mp_chmap_sel_get_def(s, &t, map->num) && test_layout(s, &t)) {
            *map = t;
            return true;
        }
    }
    for (int i = 0; i < MP_ARRAY_SIZE(speaker_replacements); i++) {
        struct mp_chmap  t = *map;
        struct mp_chmap *r = (struct mp_chmap *)speaker_replacements[i];
        if (replace_speakers(&t, r) && test_layout(s, &t)) {
            *map = t;
            return true;
        }
    }

    if (mp_chmap_sel_fallback(s, map))
        return true;

    // Fallback to mono/stereo as last resort
    *map = (struct mp_chmap) MP_CHMAP_INIT_STEREO;
    if (test_layout(s, map))
        return true;
    *map = (struct mp_chmap) MP_CHMAP_INIT_MONO;
    if (test_layout(s, map))
        return true;
    *map = (struct mp_chmap) {0};
    return false;
}

// Determine which channel map to use given a source channel map using a
// fallback algorithm that prefers upmix
bool mp_chmap_sel_fallback(const struct mp_chmap_sel *s, struct mp_chmap *map)
{
    int best_upmix_diff   = INT_MAX;
    int best_downmix_diff = INT_MAX;
    struct mp_chmap best_upmix;
    struct mp_chmap best_downmix;

    for (int n = 0; n < s->num_chmaps; n++) {
        struct mp_chmap e = s->chmaps[n];
        struct mp_chmap diff;

        mp_chmap_diff(&e, map, &diff);
        if (mp_chmap_contains(&e, map) && best_upmix_diff > diff.num) {
            best_upmix_diff = diff.num;
            best_upmix = e;
        }

        mp_chmap_diff(map, &e, &diff);
        if (mp_chmap_contains(map, &e) && best_downmix_diff > diff.num) {
            best_downmix_diff = diff.num;
            best_downmix = e;
        }
    }

    if (best_upmix_diff < INT_MAX) {
        *map = best_upmix;
        return true;
    }

    if (best_downmix_diff < INT_MAX) {
        *map = best_downmix;
        return true;
    }
    return false;
}

// Set map to a default layout with num channels. Used for audio APIs that
// return a channel count as part of format negotiation, but give no
// information about the channel layout.
// If the channel count is correct, do nothing and leave *map untouched.
bool mp_chmap_sel_get_def(const struct mp_chmap_sel *s, struct mp_chmap *map,
                          int num)
{
    if (map->num != num) {
        *map = (struct mp_chmap) {0};
        // Set of speakers or waveext might allow it.
        struct mp_chmap t;
        mp_chmap_from_channels(&t, num);
        mp_chmap_reorder_to_waveext(&t);
        if (test_layout(s, &t)) {
            *map = t;
        } else {
            for (int n = 0; n < s->num_chmaps; n++) {
                if (s->chmaps[n].num == num) {
                    *map = s->chmaps[n];
                    break;
                }
            }
        }
    }
    return map->num > 0;
}
