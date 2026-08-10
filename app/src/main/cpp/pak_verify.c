/*
 * pak_verify.c - validation of the asset pack directory.
 *
 * Written in plain C on purpose. This is the one place where the engine reads
 * a length-prefixed binary blob and trusts it enough to hand pointers into it
 * to the GL driver and the audio mixer, so it wants to be a small, dependency
 * free, easily audited function with no allocation, no exceptions and no
 * templates in sight. Every field is checked against the real mapped size
 * before anything downstream believes it.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define PAK_MAGIC     0x4B415048u   /* 'HPAK' */
#define PAK_VERSION   3u
#define PAK_NAME_LEN  56
#define PAK_MAX_ENTRIES 100000u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t entryCount;
    uint32_t reserved;
} PakHeaderC;

typedef struct {
    char     name[PAK_NAME_LEN];
    uint32_t type;
    uint32_t w;
    uint32_t h;
    uint32_t mips;
    uint64_t offset;
    uint64_t size;
} PakEntryC;

/*
 * Returns 1 if the pack is structurally sound, 0 otherwise. On failure `err`
 * (if non-NULL) receives a short human-readable reason.
 */
int hm_pak_validate(const void* base, size_t size, char* err, size_t errLen)
{
    const PakHeaderC* hdr;
    const PakEntryC* entries;
    size_t dirBytes;
    uint32_t i;

#define FAIL(msg) do {                                   \
        if (err && errLen) snprintf(err, errLen, "%s", msg); \
        return 0;                                        \
    } while (0)

    if (base == NULL) FAIL("null mapping");
    if (size < sizeof(PakHeaderC)) FAIL("file smaller than header");

    hdr = (const PakHeaderC*)base;
    if (hdr->magic != PAK_MAGIC) FAIL("bad magic");
    if (hdr->version != PAK_VERSION) FAIL("unsupported version");
    if (hdr->entryCount > PAK_MAX_ENTRIES) FAIL("absurd entry count");

    /* Overflow-safe: entryCount is bounded above, so this cannot wrap. */
    dirBytes = sizeof(PakHeaderC) + (size_t)hdr->entryCount * sizeof(PakEntryC);
    if (dirBytes > size) FAIL("directory extends past end of file");

    entries = (const PakEntryC*)((const uint8_t*)base + sizeof(PakHeaderC));

    for (i = 0; i < hdr->entryCount; i++) {
        const PakEntryC* e = &entries[i];
        uint64_t end;

        /* The name is handed to strncmp later, so it must be terminated. */
        if (memchr(e->name, '\0', PAK_NAME_LEN) == NULL) FAIL("unterminated entry name");

        if (e->offset < dirBytes) FAIL("entry overlaps the directory");
        end = e->offset + e->size;
        if (end < e->offset) FAIL("entry size overflows");
        if (end > (uint64_t)size) FAIL("entry extends past end of file");

        /* Textures: the declared dimensions must match the payload exactly,
         * because the upload path passes `size` straight to the driver. */
        if (e->type == 1u || e->type == 2u) {
            uint64_t need;
            if (e->w == 0u || e->h == 0u) FAIL("texture with zero dimension");
            if (e->w > 8192u || e->h > 8192u) FAIL("texture too large");
            need = (uint64_t)e->w * (uint64_t)e->h * 4u;
            if (need != e->size) FAIL("texture size does not match dimensions");
        }

        /* Audio: `w` is the sample rate and `h` the frame count. */
        if (e->type == 3u || e->type == 4u) {
            uint64_t channels = (e->type == 4u) ? 2u : 1u;
            uint64_t need = (uint64_t)e->h * channels * 2u;
            if (e->w < 4000u || e->w > 192000u) FAIL("implausible sample rate");
            if (need != e->size) FAIL("audio size does not match frame count");
        }
    }

#undef FAIL
    return 1;
}
