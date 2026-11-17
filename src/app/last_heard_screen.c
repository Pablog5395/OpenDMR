/*
 * Copyright (C) 2026 Junqiang Shi, VA3BPF / BG8AM
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
 * following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
 *    disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
 *    following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote
 *    products derived from this software without specific prior written permission.
 *
 * 4. Use of this source code or binary releases for commercial purposes is strictly forbidden without the written
 *    permission of the copyright holder. This includes, without limitation, incorporation in a commercial product or
 *    incorporation into a product or project which allows commercial use.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * OpenDMR, screen: the last heard list (menuLastHeard.c, DM-32 layout, docs/ui-design.md 5.8). Two-line rows:
 * the caller ("callsign first name"), under it the talk group or the private call and the timeslot, on the right how
 * long ago. A caller whose call carried a position shows the distance and direction from ours after the timeslot
 * (position.c). UP / DOWN move (with SK2: to the first / the last), the left key transmits to that talk group from now
 * on (with SK2: a private call to the caller), # held clears the list. Again once a minute (the times) and when the
 * list changes (UI_DATA_LAST_HEARD). Nothing is heard until the DMR receive comes: "List empty".
 */

#include <stdio.h>
#include <string.h>
#include "widgets.h"
#include "codeplug_text.h"
#include "settings.h"
#include "codeplug.h"
#include "systime.h"
#include "last_heard.h"
#include "last_heard_format.h"
#include "contacts.h"
#include "position.h"
#include "last_heard_screen.h"

#define REFRESH_MS      60000 // the times are in minutes
#define CALLER_SIZE     48
#define SUB_SIZE        48
#define ELAPSED_SIZE    16

static last_heard_entry_t entry; // 80 bytes: not on the UI task stack
static uint32_t own_id;
static int first; // the entry on the top row

// The selection inside the list, and on the rows
static void clamp(int32_t *selected)
{
    int count = last_heard_count();

    if (*selected >= count) {
        *selected = (count > 0) ? (count - 1) : 0;
    }
    if (*selected < first) {
        first = *selected;
    } else if (*selected >= (first + LIST_VIEW_ROWS2)) {
        first = *selected - (LIST_VIEW_ROWS2 - 1);
    }
}

static void enter(void *arg)
{
    (void)arg;
    own_id = codeplug_get_dmr_id();
    clamp(ui_state());
    ui_timer_start(REFRESH_MS);
}

static void select_for_tx(int32_t selected, bool private_call)
{
    if (!last_heard_get(selected, &entry)) {
        return;
    }

    uint32_t id = private_call ? entry.id : entry.tg_or_pc;
    bool pc = private_call || ((entry.tg_or_pc >> 24) == LAST_HEARD_PC_FLAG);

    contacts_set_tx(id, pc, ((entry.timeslot <= 1) ? (entry.timeslot + 1) : 0));
    ui_home();
}

static bool handle(const ui_event_t *ev)
{
    int32_t *selected = ui_state();
    int count = last_heard_count();
    bool sk2 = (ev->modifiers & UI_MODIFIER_SK2) != 0;

    if (ev->type == UI_EVENT_TIMER) {
        ui_timer_start(REFRESH_MS);
        return true;
    }
    if (ev->type == UI_EVENT_DATA) {
        if (ev->what & UI_DATA_LAST_HEARD) {
            clamp(selected);
            return true;
        }
        return false;
    }
    if (((ev->type == UI_EVENT_KEY_PRESS) || (ev->type == UI_EVENT_KEY_REPEAT)) && (count > 0)) {
        // As the original: no wrapping
        if (ev->key == UI_KEY_DOWN) {
            *selected = sk2 ? (count - 1) : ((*selected < (count - 1)) ? (*selected + 1) : *selected);
            clamp(selected);
            return true;
        }
        if (ev->key == UI_KEY_UP) {
            *selected = (sk2 || (*selected == 0)) ? 0 : (*selected - 1);
            clamp(selected);
            return true;
        }
        if ((ev->key == UI_KEY_GREEN) && (ev->type == UI_EVENT_KEY_PRESS)) {
            select_for_tx(*selected, sk2);
            return false;
        }
    }
    if ((ev->type == UI_EVENT_KEY_LONG) && (ev->key == UI_KEY_HASH) && !(ev->modifiers & UI_MODIFIER_SK1) && !sk2) {
        last_heard_clear(); // its UI_DATA_LAST_HEARD brings the redraw
        *selected = 0;
        first = 0;
        return true;
    }
    list_handle_back(ev);
    return false;
}

// The channel screen's recent callers and this list show the same caller text (last_heard_format.c)

static void draw_row(int row, const last_heard_entry_t *e, bool selected, uint32_t now)
{
    char caller[CALLER_SIZE];
    char sub[SUB_SIZE];
    char elapsed[ELAPSED_SIZE];
    char location[24];
    uint32_t called = e->tg_or_pc & CODEPLUG_CONTACT_ID_MAX;
    int length;

    last_heard_format_caller(e, caller, sizeof(caller));
    if ((e->tg_or_pc >> 24) == LAST_HEARD_PC_FLAG) {
        if (called == own_id) {
            length = snprintf(sub, sizeof(sub), "%s %s", text(TEXT_PRIVATE_CALL), text(TEXT_UI_ME));
        } else {
            length = snprintf(sub, sizeof(sub), "%s %lu", text(TEXT_PRIVATE_CALL), (unsigned long)called);
        }
    } else {
        length = snprintf(sub, sizeof(sub), "%s %lu", text(TEXT_TG), (unsigned long)called);
    }
    if ((e->timeslot <= 1) && (length > 0) && (length < (int)sizeof(sub))) {
        snprintf(&sub[length], sizeof(sub) - length, "  TS%u", (unsigned)(e->timeslot + 1));
    }
    if (e->has_location) {
        size_t used = strlen(sub);

        position_format_short(location, sizeof(location), e->latitude_e5, e->longitude_e5);
        if (used < (sizeof(sub) - 1)) {
            snprintf(&sub[used], sizeof(sub) - used, "  %s", location);
        }
    }
    last_heard_format_elapsed(e, now, elapsed, sizeof(elapsed));

    list_view_row_t data = {
        .icon = ICONS_CONTACT,
        .name = caller,
        .value = elapsed,
        .sub = sub,
        .selected = selected,
    };

    list_view_draw_row2(LIST_VIEW_TOP + (row * LIST_VIEW_ROW2_HEIGHT), &data);
}

static void draw(void)
{
    int32_t selected = *ui_state();
    int count = last_heard_count();
    uint32_t now = systime_get_ms();
    char position[24];

    snprintf(position, sizeof(position), "%d/%d", (int)(selected + 1), count);
    list_view_draw_title(text(TEXT_LAST_HEARD), ((count > 0) ? position : NULL));
    bottom_bar_draw_soft_keys(text(TEXT_UI_OK), text(TEXT_UI_BACK));

    if (count == 0) {
        list_view_row_t row = {.icon = ICONS_COUNT, .name = text(TEXT_LIST_EMPTY), .dimmed = true};

        list_view_draw_row(0, &row);
        return;
    }
    for (int row = 0; row < LIST_VIEW_ROWS2; row++) {
        if (!last_heard_get(first + row, &entry)) {
            break;
        }
        draw_row(row, &entry, ((first + row) == selected), now);
    }
}

const ui_screen_t last_heard_screen = {
    .enter = enter,
    .handle = handle,
    .draw = draw,
};
