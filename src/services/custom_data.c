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
 * OpenDMR, service: the OpenGD77 custom data blocks of the codeplug (the main firmware's codeplugGetCustomData()
 * / codeplugSetCustomData()): satellite keps, themes, and settings of ours (keymap, skip list, ...).
 *
 * An area without the "OpenGD77" header (a radio from the factory, after another firmware, or whose codeplug our CPS
 * wrote without custom data) gets one at the first write, as the CPS makes it (codeplug.js storeCustomBlock()):
 * else the settings kept there were never saved (#210).
 */

#include <string.h>
#include "flash.h"
#include "custom_data.h"

#define BASE        (128 * 1024) // the "flash" part of the codeplug (codeplug.c FLASH_PART_OFFSET)
#define FIRST_BLOCK 12           // after the magic
#define END         0x10000      // the blocks are below this
#define EMPTY       0xFFFFFFFFUL
#define VERSION     1            // after the magic, as the CPS writes it

typedef struct {
    uint32_t type;
    uint32_t length;
} header_t;

static bool has_header(void)
{
    char magic[8];

    return flash_read(BASE, (uint8_t *)magic, sizeof(magic)) && (memcmp(magic, "OpenGD77", 8) == 0);
}

// The header written, the area after it cleared (whatever another firmware left there would read as blocks). From
// the last sector to the first: a power cut before the header leaves the area without one, as before
static bool make_header(void)
{
    static const uint8_t header[FIRST_BLOCK] = {'O', 'p', 'e', 'n', 'G', 'D', '7', '7', VERSION, 0, 0, 0};
    uint8_t erased[64];
    bool ok = true;

    memset(erased, 0xFF, sizeof(erased));
    for (uint32_t sector = END; sector > 0;) {
        sector -= FLASH_SECTOR_SIZE;
        flash_batch_start(); // one sector write, none when it is erased already
        for (uint32_t at = 0; at < FLASH_SECTOR_SIZE; at += sizeof(erased)) {
            ok = flash_write(BASE + sector + at, erased, sizeof(erased)) && ok;
        }
        if (sector == 0) {
            ok = flash_write(BASE, header, sizeof(header)) && ok;
        }
        ok = flash_batch_end() && ok;
    }
    return ok;
}

// The address of the block of that type, from BASE (EMPTY: the first empty one); 0: none
static uint32_t find_block(uint32_t type, header_t *header)
{
    uint32_t address = FIRST_BLOCK;

    if (!has_header()) {
        return 0;
    }

    while (address < END) {
        if (!flash_read(BASE + address, (uint8_t *)header, sizeof(*header))) {
            return 0;
        }
        if (header->type == type) {
            return address;
        }
        if ((header->length == 0) || (header->length == EMPTY)) {
            return 0;
        }
        address += sizeof(*header) + header->length;
    }
    return 0;
}

uint32_t custom_data_find(uint32_t type, uint32_t *length)
{
    header_t header;
    uint32_t address = find_block(type, &header);

    if (address == 0) {
        return 0;
    }
    if (length) {
        *length = header.length;
    }
    return BASE + address + sizeof(header);
}

bool custom_data_read(uint32_t type, uint8_t *data, uint32_t length)
{
    header_t header;
    uint32_t address = find_block(type, &header);

    return (address != 0) && (header.length == length) && flash_read(BASE + address + sizeof(header), data, length);
}

bool custom_data_write(uint32_t type, const uint8_t *data, uint32_t length)
{
    header_t header;
    uint32_t address = find_block(type, &header);
    bool ok;

    if (address == 0) { // a new one goes into the first empty slot
        if (!has_header() && !make_header()) {
            return false;
        }
        address = find_block(EMPTY, &header);
        if ((address == 0) || (header.length != EMPTY) || ((address + sizeof(header) + length) >= END)) {
            return false;
        }
        header.type = type;
        header.length = length;
    }
    if (header.length != length) {
        return false;
    }

    flash_batch_start();
    ok = flash_write(BASE + address, (const uint8_t *)&header, sizeof(header));
    ok = flash_write(BASE + address + sizeof(header), data, length) && ok;
    return flash_batch_end() && ok;
}
