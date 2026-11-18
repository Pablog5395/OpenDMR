/*
 * Copyright (C) 2026 Pablo Gualtieri (Pablog5395)
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
 * OpenDMR, the caller text of the last heard entries (last_heard_screen.c and the channel screen's recent callers):
 * "callsign first name" from the contacts or the DMR IDs, else the caller's talker alias (the alias first with the TA
 * priorities of the settings), else the DMR ID.
 */

#include <stdio.h>
#include <string.h>
#include "codeplug_text.h"
#include "settings.h"
#include "codeplug.h"
#include "text.h"
#include "last_heard.h"
#include "last_heard_format.h"

#define TA_PRIORITY_MIN 2 // settings.contact_display_priority from here: the talker alias first (TA_CC_DB, TA_DB_CC)

// "callsign first name" from "callsign name ..."; "callsign DMR ID: ..." gives the callsign (splitCaller())
static void caller_title(const char *from, char *title, size_t size)
{
    const char *space = strchr(from, ' ');

    if (!space || (space == from)) {
        snprintf(title, size, "%s", from);
        return;
    }

    int callsign_length = (int)(space - from);
    const char *rest = space + 1;

    if (strncmp(rest, "DMR ID:", 7) == 0) {
        snprintf(title, size, "%.*s", callsign_length, from);
        return;
    }

    const char *name_end = strchr(rest, ' ');
    int name_length = name_end ? (int)(name_end - rest) : (int)strlen(rest);

    snprintf(title, size, "%.*s %.*s", callsign_length, from, name_length, rest);
}

void last_heard_format_caller(const last_heard_entry_t *entry, char *out, size_t size)
{
    bool unknown = (entry->text[0] == 0) || (strncmp(entry->text, "ID:", 3) == 0);
    bool use_alias =
        (entry->talker_alias[0] != 0) && ((settings.contact_display_priority >= TA_PRIORITY_MIN) || unknown);

    if (use_alias) {
        caller_title(entry->talker_alias, out, size);
    } else if (entry->text[0] != 0) {
        caller_title(entry->text, out, size);
    } else {
        static codeplug_contact_t contact; // 28 bytes: not on the stack
        int index = codeplug_contact_find(entry->id, CODEPLUG_CALL_PRIVATE, 0);

        if ((index > 0) && codeplug_get_contact(index, &contact)) {
            codeplug_text_name(contact.name, CODEPLUG_NAME_LENGTH, out, size);
        } else {
            snprintf(out, size, "ID:%lu", (unsigned long)entry->id);
        }
    }
}

void last_heard_format_elapsed(const last_heard_entry_t *entry, uint32_t now, char *out, size_t size)
{
    uint32_t minutes = (now - entry->time_ms) / 60000UL;

    if (minutes < 60) {
        snprintf(out, size, "%lu %s", (unsigned long)minutes, text(TEXT_UI_MINUTES_SHORT));
    } else {
        snprintf(out, size, "%lu %s", (unsigned long)((minutes < (999 * 60)) ? (minutes / 60) : 999),
                 text(TEXT_UI_HOURS_SHORT));
    }
}
