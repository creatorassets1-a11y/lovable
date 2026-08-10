// pack.h - the on-disk asset pack format, shared by the baker and the runtime.
//
// One flat file, uncompressed, 16-byte aligned. It is stored uncompressed in
// the APK too (see aaptOptions noCompress in app/build.gradle) so the runtime
// can mmap it and hand pixel data straight to the driver with no inflate step
// and no second copy. That costs download size and buys load time.
#pragma once
#include <cstdint>
#include <cstring>

namespace hm {

const uint32_t PAK_MAGIC = 0x4B415048u;   // 'HPAK'
const uint32_t PAK_VERSION = 3;
const int PAK_NAME_LEN = 56;

enum PakType : uint32_t {
    PAK_TEX_ALBEDO = 1,   // RGBA8, w*h*4
    PAK_TEX_NORMAL = 2,   // RGBA8: xyz in rgb, roughness in a
    PAK_AUDIO_MONO = 3,   // int16 PCM, `w` = sample rate, `h` = frame count
    PAK_AUDIO_STEREO = 4, // int16 PCM interleaved
};

struct PakHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t entryCount;
    uint32_t reserved;
};

struct PakEntry {
    char     name[PAK_NAME_LEN];
    uint32_t type;
    uint32_t w;        // texture width, or sample rate for audio
    uint32_t h;        // texture height, or frame count for audio
    uint32_t mips;     // mip levels present (textures), 0 = base only
    uint64_t offset;   // from start of file
    uint64_t size;     // bytes
};

inline uint64_t pakAlign(uint64_t v) { return (v + 15ull) & ~15ull; }

} // namespace hm
