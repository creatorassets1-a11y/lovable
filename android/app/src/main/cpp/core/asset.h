// Asset access. Backed by AAssetManager on device and by the filesystem in the
// host preview harness, so every other file can stay platform-agnostic.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace hl {

/** Install the platform backend once at startup. */
void asset_set_android(void* asset_manager);
void asset_set_directory(const std::string& dir);

/** Whole-file read. Returns empty on failure; callers must check. */
std::vector<uint8_t> asset_read(const std::string& path);
std::string asset_read_text(const std::string& path);
bool asset_exists(const std::string& path);

/** Haptics.
 *
 * The engine never calls into Java. It pushes a request here and the platform
 * layer drains the queue on its own thread, which avoids attaching the audio or
 * GL thread to the JVM just to buzz a motor. */
void push_haptic(int ms, int amplitude);
/** Returns ms | (amplitude << 16), or 0 when nothing is pending. */
int pop_haptic();

void logi(const char* fmt, ...);
void loge(const char* fmt, ...);

}  // namespace hl
