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
 * OpenDMR, service: the power-on PIN and the lock PIN (security.h).
 *
 * Both switches and the PIN are ours, in a block after the settings (their flash sector, behind the main firmware's
 * settings_t: 0x604B - 0x60B6). The CPS can't change it: its sector writes keep the block (security_keep_block()),
 * and while the radio is secured the cable link answers nothing at all. A firmware update keeps it too.
 *
 * A forgotten PIN: tools/debug/pin_wipe.py, through the chip's bootloader at power on (not this firmware): it blanks
 * this block only, the codeplug and the settings stay. Note that the bootloader reads and writes
 * the whole data flash for anyone with the cable (tools/backup/dm32_dump.py): the PINs keep the radio from being
 * used, not its flash from being read.
 */

#include <string.h>
#include "flash.h"
#include "systime.h"
#include "datetime.h"
#include "security.h"

#define ADDRESS     0x6100     // our block, in the data flash window (tools/debug/pin_wipe.py: 24 bytes)
#define MAGIC       0x354E4950 // "PIN5"
#define HASH_ROUNDS 4096       // a few ms; with 4 - 6 digits a dump of the flash is guessed anyway: not in clear

#define FLAG_BOOT 0x01
#define FLAG_LOCK 0x02

// The PIN is not kept: an iterated FNV-1a 64 of its digits, a random salt and this radio's flash ID (the block
// copied to another radio doesn't verify there)
typedef struct {
    uint32_t magic;
    uint8_t flags;
    uint8_t set; // a PIN is there
    uint8_t reserved[2];
    uint32_t salt;
    uint32_t reserved2;
    uint64_t hash;
} block_t;

_Static_assert(sizeof(block_t) == 24, "tools/debug/pin_wipe.py blanks 24 bytes");

static block_t block;
static uint64_t radio_id; // flash_unique_id()
static volatile bool boot_locked = false;
static volatile bool locked = false;

static bool save(void)
{
    block.magic = MAGIC;
    return flash_write(ADDRESS, (const uint8_t *)&block, sizeof(block));
}

static uint64_t pin_hash(uint32_t salt, const char *digits)
{
    uint64_t hash = 14695981039346656037ULL;

    for (int round = 0; round < HASH_ROUNDS; round++) {
        for (int i = 0; i < 4; i++) {
            hash = (hash ^ ((salt >> (8 * i)) & 0xFF)) * 1099511628211ULL;
        }
        for (int i = 0; i < 8; i++) {
            hash = (hash ^ ((radio_id >> (8 * i)) & 0xFF)) * 1099511628211ULL;
        }
        for (const char *d = digits; *d; d++) {
            hash = (hash ^ (uint8_t)*d) * 1099511628211ULL;
        }
    }
    return hash;
}

void security_load(void)
{
    radio_id = flash_unique_id();
    if (!flash_read(ADDRESS, (uint8_t *)&block, sizeof(block)) || (block.magic != MAGIC) || (block.set > 1)) {
        memset(&block, 0, sizeof(block));
    }
    if (!block.set) {
        block.flags = 0;
    }
    boot_locked = (block.flags & FLAG_BOOT) != 0;
}

bool security_has_pin(void)
{
    return (block.flags & (FLAG_BOOT | FLAG_LOCK)) != 0;
}

bool security_check_pin(const char *digits)
{
    size_t length = strlen(digits);

    return block.set && (length >= SECURITY_PIN_MIN) && (length <= SECURITY_PIN_MAX) &&
           (pin_hash(block.salt, digits) == block.hash);
}

bool security_set_pin(const char *digits)
{
    size_t length = strlen(digits);
    block_t before = block;

    if ((length < SECURITY_PIN_MIN) || (length > SECURITY_PIN_MAX)) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        if ((digits[i] < '0') || (digits[i] > '9')) {
            return false;
        }
    }
    block.salt = (systime_get_ms() * 2654435761UL) ^ datetime_now() ^ (uint32_t)(uintptr_t)&before;
    block.hash = pin_hash(block.salt, digits);
    block.set = 1;
    if (!save()) {
        block = before;
        return false;
    }
    return true;
}

static void set_flag(uint8_t flag, bool on)
{
    uint8_t flags = on ? (uint8_t)(block.flags | flag) : (uint8_t)(block.flags & ~flag);

    if (on && !block.set) {
        return;
    }
    if (flags != block.flags) {
        uint8_t before = block.flags;

        block.flags = flags;
        if (!save()) {
            block.flags = before;
        }
    }
}

bool security_boot_pin_on(void)
{
    return (block.flags & FLAG_BOOT) != 0;
}

bool security_lock_pin_on(void)
{
    return (block.flags & FLAG_LOCK) != 0;
}

void security_set_boot_pin(bool on)
{
    set_flag(FLAG_BOOT, on);
}

void security_set_lock_pin(bool on)
{
    set_flag(FLAG_LOCK, on);
}

bool security_secured(void)
{
    return boot_locked || (locked && security_lock_pin_on());
}

// The CPS writes a 4 KB sector of the window: ours stays as it is
void security_keep_block(uint32_t sector_address, uint8_t *sector)
{
    if ((ADDRESS >= sector_address) && ((ADDRESS + sizeof(block)) <= (sector_address + FLASH_SECTOR_SIZE))) {
        block_t stored;

        if (flash_read(ADDRESS, (uint8_t *)&stored, sizeof(stored))) {
            memcpy(&sector[ADDRESS - sector_address], &stored, sizeof(stored));
        }
    }
}

bool security_boot_locked(void)
{
    return boot_locked;
}

void security_boot_unlocked(void)
{
    boot_locked = false;
}

void security_set_locked(bool on)
{
    locked = on;
}
