#include "assets_fs.h"

#include <FS.h>
#include <LittleFS.h>
#include <lvgl.h>

extern "C" {
unsigned lodepng_decode32(unsigned char ** out, unsigned * w, unsigned * h, const unsigned char * in, size_t insize);
const char * lodepng_error_text(unsigned code);
}

namespace {
constexpr char kFsLetter = 'S';
constexpr uint32_t kPngSignatureA = 0x89504E47UL;
constexpr uint32_t kPngSignatureB = 0x0D0A1A0AUL;

static lv_fs_drv_t s_lv_fs_drv;
static bool s_driver_registered = false;
static bool s_fs_ready = false;
static bool s_png_decoder_registered = false;

const char * kExpectedAssets[] = {
    "/assets/logo_main_240x120.png",
    "/assets/logo_main_120x60.png",
    "/assets/splash_screen_320x240.png",
    "/assets/background_main_320x240.png",
    "/assets/menu_stages_64.png",
    "/assets/menu_live_64.png",
    "/assets/menu_calibration_64.png",
    "/assets/menu_settings_64.png",
    "/assets/menu_alarms_64.png",
    "/assets/menu_maintenance_64.png",
    "/assets/stage_1_64.png",
    "/assets/stage_2_64.png",
    "/assets/stage_3_64.png",
    "/assets/stage_4_64.png",
    "/assets/stage_5_64.png",
    "/assets/icon_ph_32.png",
    "/assets/icon_ec_32.png",
    "/assets/icon_temp_water_32.png",
    "/assets/icon_temp_air_32.png",
    "/assets/icon_humidity_32.png",
    "/assets/icon_level_32.png",
    "/assets/icon_pump_32.png",
    "/assets/icon_recirculation_32.png",
    "/assets/icon_fill_32.png",
    "/assets/icon_light_32.png",
    "/assets/icon_fan_in_32.png",
    "/assets/icon_fan_out_32.png",
    "/assets/status_on_32.png",
    "/assets/status_off_32.png",
    "/assets/status_alert_32.png",
    "/assets/nav_back_32.png",
    "/assets/nav_home_32.png",
    "/assets/nav_next_32.png",
};

uint32_t read_be32(const uint8_t * data) {
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           static_cast<uint32_t>(data[3]);
}

bool has_png_extension(const char * path) {
    if (path == nullptr) {
        return false;
    }

    String value(path);
    value.toLowerCase();
    return value.endsWith(".png");
}

String short_path(const String & path) {
    const int idx = path.lastIndexOf('/');
    if (idx >= 0 && idx + 1 < path.length()) {
        return path.substring(idx + 1);
    }
    return path;
}

String normalize_path_internal(const char * path) {
    if (path == nullptr || path[0] == '\0') {
        return String();
    }

    String normalized(path);

    if (normalized.length() >= 2 && normalized[1] == ':') {
        normalized.remove(0, 2);
    }

    if (!normalized.startsWith("/")) {
        normalized = "/" + normalized;
    }

    while (normalized.indexOf("//") >= 0) {
        normalized.replace("//", "/");
    }

    return normalized;
}

void log_file_recursive(const String & path, uint8_t depth = 0) {
    File entry = LittleFS.open(path, "r");
    if (!entry) {
        Serial.printf("LIST FAIL: %s\n", path.c_str());
        return;
    }

    if (!entry.isDirectory()) {
        Serial.printf("FOUND: %s (%u bytes)\n", path.c_str(), static_cast<unsigned>(entry.size()));
        entry.close();
        return;
    }

    Serial.printf("DIR: %s\n", path.c_str());
    File child = entry.openNextFile();
    while (child) {
        String child_path = path;
        if (!child_path.endsWith("/")) {
            child_path += "/";
        }
        child_path += child.name();

        if (child.isDirectory()) {
            child.close();
            log_file_recursive(child_path, depth + 1);
        } else {
            Serial.printf("FOUND: %s (%u bytes)\n", child_path.c_str(), static_cast<unsigned>(child.size()));
            child.close();
        }
        child = entry.openNextFile();
    }
    entry.close();
}

AssetProbeResult probe_png_internal(const char * lvgl_path) {
    AssetProbeResult result;
    result.normalized_path = normalize_path_internal(lvgl_path);

    if (!s_fs_ready) {
        result.error = "LittleFS not mounted";
        return result;
    }

    if (result.normalized_path.isEmpty()) {
        result.error = "Empty path";
        return result;
    }

    if (!has_png_extension(result.normalized_path.c_str())) {
        result.error = "Not a PNG path";
        return result;
    }

    File file = LittleFS.open(result.normalized_path, "r");
    if (!file) {
        result.error = "File open failed";
        return result;
    }

    result.file_size = static_cast<size_t>(file.size());
    if (result.file_size < 24) {
        result.error = "PNG too small";
        file.close();
        return result;
    }

    uint8_t encoded[24] = {};
    const size_t bytes_read = file.read(encoded, sizeof(encoded));
    file.close();
    if (bytes_read != sizeof(encoded)) {
        result.error = "Short read from filesystem";
        return result;
    }

    const uint32_t sig_a = read_be32(encoded);
    const uint32_t sig_b = read_be32(encoded + 4);
    if (sig_a != kPngSignatureA || sig_b != kPngSignatureB) {
        result.error = "PNG signature mismatch";
        return result;
    }

    if (read_be32(encoded + 12) != 0x49484452UL) {
        result.error = "PNG missing IHDR";
        return result;
    }

    result.ok = true;
    result.width = read_be32(encoded + 16);
    result.height = read_be32(encoded + 20);
    if (result.width == 0 || result.height == 0) {
        result.ok = false;
        result.error = "PNG invalid dimensions";
    }
    return result;
}

void convert_color_depth(uint8_t * img, uint32_t px_cnt) {
#if LV_COLOR_DEPTH == 32
    lv_color32_t * img_argb = reinterpret_cast<lv_color32_t *>(img);
    lv_color_t color;
    lv_color_t * img_color = reinterpret_cast<lv_color_t *>(img);
    for (uint32_t index = 0; index < px_cnt; ++index) {
        color = lv_color_make(img_argb[index].ch.red, img_argb[index].ch.green, img_argb[index].ch.blue);
        img_color[index].ch.red = color.ch.blue;
        img_color[index].ch.blue = color.ch.red;
    }
#elif LV_COLOR_DEPTH == 16
    lv_color32_t * img_argb = reinterpret_cast<lv_color32_t *>(img);
    lv_color_t color;
    for (uint32_t index = 0; index < px_cnt; ++index) {
        color = lv_color_make(img_argb[index].ch.blue, img_argb[index].ch.green, img_argb[index].ch.red);
        img[index * 3 + 2] = img_argb[index].ch.alpha;
        img[index * 3 + 1] = color.full >> 8;
        img[index * 3 + 0] = color.full & 0xFF;
    }
#elif LV_COLOR_DEPTH == 8
    lv_color32_t * img_argb = reinterpret_cast<lv_color32_t *>(img);
    lv_color_t color;
    for (uint32_t index = 0; index < px_cnt; ++index) {
        color = lv_color_make(img_argb[index].ch.red, img_argb[index].ch.green, img_argb[index].ch.blue);
        img[index * 2 + 1] = img_argb[index].ch.alpha;
        img[index * 2 + 0] = color.full;
    }
#elif LV_COLOR_DEPTH == 1
    lv_color32_t * img_argb = reinterpret_cast<lv_color32_t *>(img);
    for (uint32_t index = 0; index < px_cnt; ++index) {
        const uint8_t brightness = img_argb[index].ch.red | img_argb[index].ch.green | img_argb[index].ch.blue;
        img[index * 2 + 1] = img_argb[index].ch.alpha;
        img[index * 2 + 0] = brightness > 128 ? 1 : 0;
    }
#endif
}

bool fs_ready_cb(lv_fs_drv_t * drv) {
    (void)drv;
    return s_fs_ready;
}

void * fs_open_cb(lv_fs_drv_t * drv, const char * path, lv_fs_mode_t mode) {
    (void)drv;

    const String normalized = normalize_path_internal(path);
    const char * open_mode = (mode & LV_FS_MODE_WR) ? "w" : "r";
    fs::File * file = new fs::File(LittleFS.open(normalized, open_mode));
    if (!file || !(*file)) {
        Serial.printf("LVGL FS OPEN FAIL: %s -> %s\n", path ? path : "(null)", normalized.c_str());
        delete file;
        return nullptr;
    }
    return file;
}

lv_fs_res_t fs_close_cb(lv_fs_drv_t * drv, void * file_p) {
    (void)drv;

    fs::File * file = static_cast<fs::File *>(file_p);
    if (file == nullptr) {
        return LV_FS_RES_INV_PARAM;
    }

    file->close();
    delete file;
    return LV_FS_RES_OK;
}

lv_fs_res_t fs_read_cb(lv_fs_drv_t * drv, void * file_p, void * buf, uint32_t btr, uint32_t * br) {
    (void)drv;

    fs::File * file = static_cast<fs::File *>(file_p);
    if (file == nullptr) {
        return LV_FS_RES_INV_PARAM;
    }

    const size_t bytes_read = file->read(static_cast<uint8_t *>(buf), btr);
    if (br != nullptr) {
        *br = static_cast<uint32_t>(bytes_read);
    }
    return LV_FS_RES_OK;
}

lv_fs_res_t fs_write_cb(lv_fs_drv_t * drv, void * file_p, const void * buf, uint32_t btw, uint32_t * bw) {
    (void)drv;

    fs::File * file = static_cast<fs::File *>(file_p);
    if (file == nullptr) {
        return LV_FS_RES_INV_PARAM;
    }

    const size_t bytes_written = file->write(static_cast<const uint8_t *>(buf), btw);
    if (bw != nullptr) {
        *bw = static_cast<uint32_t>(bytes_written);
    }
    return LV_FS_RES_OK;
}

lv_fs_res_t fs_seek_cb(lv_fs_drv_t * drv, void * file_p, uint32_t pos, lv_fs_whence_t whence) {
    (void)drv;

    fs::File * file = static_cast<fs::File *>(file_p);
    if (file == nullptr) {
        return LV_FS_RES_INV_PARAM;
    }

    SeekMode mode = SeekSet;
    if (whence == LV_FS_SEEK_CUR) {
        mode = SeekCur;
    } else if (whence == LV_FS_SEEK_END) {
        mode = SeekEnd;
    }

    return file->seek(pos, mode) ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}

lv_fs_res_t fs_tell_cb(lv_fs_drv_t * drv, void * file_p, uint32_t * pos_p) {
    (void)drv;

    fs::File * file = static_cast<fs::File *>(file_p);
    if (file == nullptr || pos_p == nullptr) {
        return LV_FS_RES_INV_PARAM;
    }

    *pos_p = static_cast<uint32_t>(file->position());
    return LV_FS_RES_OK;
}

lv_res_t png_decoder_info_cb(struct _lv_img_decoder_t * decoder, const void * src, lv_img_header_t * header) {
    (void)decoder;

    if (lv_img_src_get_type(src) != LV_IMG_SRC_FILE) {
        return LV_RES_INV;
    }

    const char * path = static_cast<const char *>(src);
    const AssetProbeResult probe = probe_png_internal(path);
    if (!probe.ok) {
        Serial.printf("PNG INFO FAIL: %s | %s\n", path, probe.error.c_str());
        return LV_RES_INV;
    }

    header->always_zero = 0;
    header->cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    header->w = static_cast<lv_coord_t>(probe.width);
    header->h = static_cast<lv_coord_t>(probe.height);
    return LV_RES_OK;
}

lv_res_t png_decoder_open_cb(lv_img_decoder_t * decoder, lv_img_decoder_dsc_t * dsc) {
    (void)decoder;

    if (dsc->src_type != LV_IMG_SRC_FILE) {
        return LV_RES_INV;
    }

    const char * path = static_cast<const char *>(dsc->src);
    const String normalized = normalize_path_internal(path);

    File file = LittleFS.open(normalized, "r");
    if (!file) {
        Serial.printf("PNG OPEN FAIL: %s -> %s\n", path, normalized.c_str());
        return LV_RES_INV;
    }

    const size_t file_size = static_cast<size_t>(file.size());
    uint8_t * encoded = static_cast<uint8_t *>(malloc(file_size));
    if (encoded == nullptr) {
        file.close();
        Serial.printf("PNG OPEN OOM: %s (%u bytes)\n", normalized.c_str(), static_cast<unsigned>(file_size));
        return LV_RES_INV;
    }

    const size_t bytes_read = file.read(encoded, file_size);
    file.close();
    if (bytes_read != file_size) {
        free(encoded);
        Serial.printf("PNG OPEN SHORT READ: %s (%u/%u)\n",
                      normalized.c_str(),
                      static_cast<unsigned>(bytes_read),
                      static_cast<unsigned>(file_size));
        return LV_RES_INV;
    }

    unsigned decoded_w = 0;
    unsigned decoded_h = 0;
    unsigned char * decoded = nullptr;
    const unsigned decode_error = lodepng_decode32(&decoded, &decoded_w, &decoded_h, encoded, file_size);
    free(encoded);
    if (decode_error != 0 || decoded == nullptr) {
        Serial.printf("PNG DECODE FAIL: %s | %u | %s\n",
                      normalized.c_str(),
                      decode_error,
                      lodepng_error_text(decode_error));
        if (decoded != nullptr) {
            free(decoded);
        }
        return LV_RES_INV;
    }

    convert_color_depth(decoded, decoded_w * decoded_h);
    dsc->img_data = decoded;
    dsc->header.always_zero = 0;
    dsc->header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    dsc->header.w = static_cast<lv_coord_t>(decoded_w);
    dsc->header.h = static_cast<lv_coord_t>(decoded_h);
    return LV_RES_OK;
}

void png_decoder_close_cb(lv_img_decoder_t * decoder, lv_img_decoder_dsc_t * dsc) {
    (void)decoder;
    if (dsc->img_data != nullptr) {
        lv_mem_free(const_cast<uint8_t *>(dsc->img_data));
        dsc->img_data = nullptr;
    }
}

void register_custom_png_decoder() {
    if (s_png_decoder_registered) {
        return;
    }

    lv_img_decoder_t * decoder = lv_img_decoder_create();
    lv_img_decoder_set_info_cb(decoder, png_decoder_info_cb);
    lv_img_decoder_set_open_cb(decoder, png_decoder_open_cb);
    lv_img_decoder_set_close_cb(decoder, png_decoder_close_cb);
    s_png_decoder_registered = true;
}
}  // namespace

String assets_fs_normalize_path(const char * path) {
    return normalize_path_internal(path);
}

bool assets_fs_file_exists(const char * lvgl_path, size_t * size_out) {
    if (!s_fs_ready) {
        return false;
    }

    const String normalized = normalize_path_internal(lvgl_path);
    File file = LittleFS.open(normalized, "r");
    if (!file) {
        return false;
    }

    if (size_out != nullptr) {
        *size_out = static_cast<size_t>(file.size());
    }
    file.close();
    return true;
}

AssetProbeResult assets_png_probe(const char * lvgl_path) {
    return probe_png_internal(lvgl_path);
}

bool assets_fs_init() {
    if (s_fs_ready) {
        return true;
    }

    s_fs_ready = LittleFS.begin(true);
    Serial.printf("LittleFS mount: %s\n", s_fs_ready ? "OK" : "FAIL");

    if (!s_fs_ready) {
        return false;
    }

    if (!s_driver_registered) {
        lv_fs_drv_init(&s_lv_fs_drv);
        s_lv_fs_drv.letter = kFsLetter;
        s_lv_fs_drv.cache_size = 0;
        s_lv_fs_drv.ready_cb = fs_ready_cb;
        s_lv_fs_drv.open_cb = fs_open_cb;
        s_lv_fs_drv.close_cb = fs_close_cb;
        s_lv_fs_drv.read_cb = fs_read_cb;
        s_lv_fs_drv.write_cb = fs_write_cb;
        s_lv_fs_drv.seek_cb = fs_seek_cb;
        s_lv_fs_drv.tell_cb = fs_tell_cb;
        lv_fs_drv_register(&s_lv_fs_drv);
        s_driver_registered = true;
    }

#if LV_USE_PNG
    Serial.println("LV_USE_PNG: 1");
#else
    Serial.println("LV_USE_PNG: 0");
#endif

    register_custom_png_decoder();
    Serial.printf("PNG decoder init: %s\n", s_png_decoder_registered ? "OK" : "FAIL");
    lv_img_cache_set_size(8);

    Serial.printf("LittleFS ready: total=%u used=%u\n",
                  static_cast<unsigned>(LittleFS.totalBytes()),
                  static_cast<unsigned>(LittleFS.usedBytes()));
    return true;
}

bool assets_fs_ready() {
    return s_fs_ready;
}

void assets_fs_log_startup_report() {
    Serial.println("=== LITTLEFS STARTUP REPORT ===");
    Serial.printf("FS READY: %s\n", s_fs_ready ? "YES" : "NO");
    Serial.printf("LVGL FS DRIVE: %c\n", kFsLetter);

    if (!s_fs_ready) {
        Serial.println("LittleFS is not mounted, report aborted.");
        return;
    }

    const char * roots[] = {
        "/assets",
        "/assets/logo",
        "/assets/backgrounds",
        "/assets/menu",
        "/assets/stages",
        "/assets/icons",
        "/assets/status",
        "/assets/navigation",
    };

    for (const char * root : roots) {
        log_file_recursive(String(root));
    }

    Serial.println("=== EXPECTED ASSET CHECK ===");
    for (const char * expected : kExpectedAssets) {
        size_t file_size = 0;
        if (assets_fs_file_exists(expected, &file_size)) {
            Serial.printf("FOUND: %s (%u bytes)\n", expected, static_cast<unsigned>(file_size));
        } else {
            Serial.printf("MISSING: %s\n", expected);
        }
    }
}
