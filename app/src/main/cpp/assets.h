// assets.h - reads the baked asset pack.
//
// The pack is stored uncompressed inside the APK, so on Android the asset
// manager hands back a pointer straight into the mapped APK. Nothing is copied
// and nothing is inflated: texture uploads and audio playback read from that
// mapping directly.
#pragma once
#include "pack.h"
#include <cstddef>

struct AAssetManager;
struct AAsset;

namespace hm {

class AssetPack {
public:
    ~AssetPack() { close(); }

#if defined(__ANDROID__)
    bool openAndroid(AAssetManager* mgr, const char* name);
#endif
    bool openFile(const char* path);     // host builds and tests
    void close();

    bool valid() const { return mBase != nullptr && mHeader != nullptr; }
    int count() const { return mHeader ? (int)mHeader->entryCount : 0; }
    const PakEntry* entryAt(int i) const;
    const PakEntry* find(const char* name) const;
    const void* dataOf(const PakEntry& e) const;
    size_t totalBytes() const { return mSize; }

private:
    const uint8_t* mBase = nullptr;
    size_t mSize = 0;
    const PakHeader* mHeader = nullptr;
    const PakEntry* mEntries = nullptr;

    AAsset* mAsset = nullptr;    // Android: keeps the mapping alive
    void* mMapped = nullptr;     // host: mmap result
    bool validateLayout();
};

} // namespace hm
