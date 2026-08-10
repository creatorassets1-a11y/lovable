#include "assets.h"
#include <cstring>
#include <cstdio>

// Structural validation lives in pak_verify.c: a small, allocation-free C
// function, because this is where we start trusting a mapped blob enough to
// hand pointers from it to the GL driver and the audio mixer.
extern "C" int hm_pak_validate(const void* base, size_t size, char* err, size_t errLen);

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#include <android/log.h>
#define ALOG(...) __android_log_print(ANDROID_LOG_INFO, "HollowAssets", __VA_ARGS__)
#define AERR(...) __android_log_print(ANDROID_LOG_ERROR, "HollowAssets", __VA_ARGS__)
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#define ALOG(...) std::printf(__VA_ARGS__), std::printf("\n")
#define AERR(...) std::printf(__VA_ARGS__), std::printf("\n")
#endif

namespace hm {

// Never trust the header's own numbers: a truncated or foreign file would
// otherwise walk us straight off the end of the mapping.
bool AssetPack::validateLayout() {
    char err[128] = {0};
    if (!hm_pak_validate(mBase, mSize, err, sizeof(err))) {
        AERR("asset pack rejected: %s", err);
        return false;
    }
    mHeader = reinterpret_cast<const PakHeader*>(mBase);
    mEntries = reinterpret_cast<const PakEntry*>(mBase + sizeof(PakHeader));
    return true;
}

#if defined(__ANDROID__)
bool AssetPack::openAndroid(AAssetManager* mgr, const char* name) {
    close();
    if (!mgr) return false;
    mAsset = AAssetManager_open(mgr, name, AASSET_MODE_BUFFER);
    if (!mAsset) { AERR("cannot open asset %s", name); return false; }
    const void* buf = AAsset_getBuffer(mAsset);
    off_t len = AAsset_getLength(mAsset);
    if (!buf || len <= 0) { AERR("cannot map asset %s", name); close(); return false; }
    mBase = reinterpret_cast<const uint8_t*>(buf);
    mSize = (size_t)len;
    if (!validateLayout()) { close(); return false; }
    ALOG("pack %s: %u entries, %.1f MB", name, mHeader->entryCount,
         mSize / (1024.0 * 1024.0));
    return true;
}
#endif

bool AssetPack::openFile(const char* path) {
    close();
#if defined(__ANDROID__)
    (void)path;
    return false;
#else
    int fd = ::open(path, O_RDONLY);
    if (fd < 0) { AERR("cannot open %s", path); return false; }
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0) { ::close(fd); return false; }
    void* p = mmap(nullptr, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (p == MAP_FAILED) { AERR("cannot map %s", path); return false; }
    mMapped = p;
    mBase = reinterpret_cast<const uint8_t*>(p);
    mSize = (size_t)st.st_size;
    if (!validateLayout()) { close(); return false; }
    return true;
#endif
}

void AssetPack::close() {
#if defined(__ANDROID__)
    if (mAsset) { AAsset_close(mAsset); mAsset = nullptr; }
#else
    if (mMapped) { munmap(mMapped, mSize); mMapped = nullptr; }
#endif
    mBase = nullptr;
    mHeader = nullptr;
    mEntries = nullptr;
    mSize = 0;
}

const PakEntry* AssetPack::entryAt(int i) const {
    if (!valid() || i < 0 || i >= (int)mHeader->entryCount) return nullptr;
    return &mEntries[i];
}

const PakEntry* AssetPack::find(const char* name) const {
    if (!valid()) return nullptr;
    for (uint32_t i = 0; i < mHeader->entryCount; i++) {
        if (std::strncmp(mEntries[i].name, name, PAK_NAME_LEN) == 0) return &mEntries[i];
    }
    return nullptr;
}

const void* AssetPack::dataOf(const PakEntry& e) const {
    if (!valid()) return nullptr;
    return mBase + e.offset;
}

} // namespace hm
