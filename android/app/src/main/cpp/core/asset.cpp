#include "asset.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#ifdef __ANDROID__
#include <android/asset_manager.h>
#include <android/log.h>
#endif

namespace hl {
namespace {
#ifdef __ANDROID__
AAssetManager* g_mgr = nullptr;
#endif
std::string g_dir;
}  // namespace

void asset_set_android(void* mgr) {
#ifdef __ANDROID__
    g_mgr = static_cast<AAssetManager*>(mgr);
#else
    (void)mgr;
#endif
}

void asset_set_directory(const std::string& dir) {
    g_dir = dir;
    if (!g_dir.empty() && g_dir.back() != '/') g_dir += '/';
}

std::vector<uint8_t> asset_read(const std::string& path) {
    std::vector<uint8_t> out;
#ifdef __ANDROID__
    if (g_mgr) {
        AAsset* a = AAssetManager_open(g_mgr, path.c_str(), AASSET_MODE_BUFFER);
        if (!a) { loge("asset missing: %s", path.c_str()); return out; }
        off_t n = AAsset_getLength(a);
        out.resize((size_t)n);
        AAsset_read(a, out.data(), (size_t)n);
        AAsset_close(a);
        return out;
    }
#endif
    FILE* f = std::fopen((g_dir + path).c_str(), "rb");
    if (!f) { loge("asset missing: %s%s", g_dir.c_str(), path.c_str()); return out; }
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n > 0) {
        out.resize((size_t)n);
        if (std::fread(out.data(), 1, (size_t)n, f) != (size_t)n) out.clear();
    }
    std::fclose(f);
    return out;
}

std::string asset_read_text(const std::string& path) {
    auto v = asset_read(path);
    return std::string(v.begin(), v.end());
}

bool asset_exists(const std::string& path) {
    return !asset_read(path).empty();
}

void logi(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
#ifdef __ANDROID__
    __android_log_vprint(ANDROID_LOG_INFO, "Matron", fmt, ap);
#else
    std::vfprintf(stdout, fmt, ap);
    std::fputc('\n', stdout);
#endif
    va_end(ap);
}

void loge(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
#ifdef __ANDROID__
    __android_log_vprint(ANDROID_LOG_ERROR, "Matron", fmt, ap);
#else
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\n', stderr);
#endif
    va_end(ap);
}

}  // namespace hl
