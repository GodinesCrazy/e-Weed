#ifndef ASSETS_FS_H
#define ASSETS_FS_H

#include <Arduino.h>

struct AssetProbeResult {
    bool ok = false;
    uint32_t width = 0;
    uint32_t height = 0;
    size_t file_size = 0;
    String normalized_path;
    String error;
};

bool assets_fs_init();
bool assets_fs_ready();

String assets_fs_normalize_path(const char * path);
bool assets_fs_file_exists(const char * lvgl_path, size_t * size_out = nullptr);
AssetProbeResult assets_png_probe(const char * lvgl_path);
void assets_fs_log_startup_report();

#endif  // ASSETS_FS_H
