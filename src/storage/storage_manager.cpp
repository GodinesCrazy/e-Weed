#include "storage/storage_manager.h"
#include <LittleFS.h>

void StorageManager::begin() {
    loadFromFS();
    last_record_ms = millis();
    last_flush_ms  = millis();
}

void StorageManager::loop() {
    const uint32_t now = millis();

    DataModel::getInstance().lock();
    uint32_t interval = DataModel::getInstance().getSettings().log_interval_ms;
    DataModel::getInstance().unlock();

    if (now - last_record_ms >= interval) {
        last_record_ms = now;
        recordSample();
    }

    if (dirty && now - last_flush_ms >= 60000) {
        last_flush_ms = now;
        flushToFS();
    }
}

void StorageManager::recordSample() {
    DataModel::getInstance().lock();
    SystemState st = DataModel::getInstance().getState();
    DataModel::getInstance().unlock();

    if (!st.telemetry_live) return;

    HistoryRecord rec;
    rec.timestamp_ms = millis();
    rec.ph           = st.ph;
    rec.tds          = st.tds;
    rec.temp_water   = st.temp_water;
    rec.temp_air     = st.temp_air;
    rec.hum_air      = st.hum_air;
    rec.level        = st.level_min ? 1 : (st.level_max ? 2 : 0);
    rec.health       = static_cast<uint8_t>(st.health);

    addRecord(rec);

    DataModel::getInstance().lock();
    DataModel::getInstance().getState().history_count = record_count;
    DataModel::getInstance().unlock();
}

void StorageManager::addRecord(const HistoryRecord &r) {
    ring_buffer[ring_head] = r;
    ring_head = (ring_head + 1) % MAX_RECORDS;
    if (record_count < MAX_RECORDS) ++record_count;
    dirty = true;
}

void StorageManager::loadFromFS() {
    File file = LittleFS.open(HISTORY_FILE, "r");
    if (!file) { Serial.println("STORE: no history file"); return; }

    file.readStringUntil('\n'); // skip header

    uint32_t loaded = 0;
    while (file.available() && loaded < MAX_RECORDS) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        HistoryRecord rec = {};
        int pos = 0, comma;

        auto nextField = [&]() -> String {
            comma = line.indexOf(',', pos);
            if (comma < 0) comma = line.length();
            String f = line.substring(pos, comma);
            pos = comma + 1;
            return f;
        };

        rec.timestamp_ms = strtoul(nextField().c_str(), nullptr, 10);
        rec.ph           = nextField().toFloat();
        rec.tds          = nextField().toInt();
        rec.temp_water   = nextField().toFloat();
        rec.temp_air     = nextField().toFloat();
        rec.hum_air      = nextField().toFloat();
        rec.level        = static_cast<uint8_t>(nextField().toInt());
        rec.health       = static_cast<uint8_t>(nextField().toInt());

        addRecord(rec);
        ++loaded;
    }
    file.close();
    dirty = false;
    Serial.printf("STORE: loaded %lu records\n", (unsigned long)loaded);
}

void StorageManager::flushToFS() {
    File file = LittleFS.open(HISTORY_FILE, "w");
    if (!file) { Serial.println("STORE: flush open fail"); return; }

    file.println("timestamp,ph,tds,tw,ta,ha,level,health");

    uint32_t start = (record_count < MAX_RECORDS) ? 0 : ring_head;
    for (uint32_t i = 0; i < record_count; ++i) {
        uint32_t idx = (start + i) % MAX_RECORDS;
        const HistoryRecord &r = ring_buffer[idx];
        char ln[128];
        snprintf(ln, sizeof(ln), "%lu,%.2f,%d,%.1f,%.1f,%.1f,%u,%u",
                 (unsigned long)r.timestamp_ms, r.ph, r.tds,
                 r.temp_water, r.temp_air, r.hum_air, r.level, r.health);
        file.println(ln);
    }
    file.close();
    dirty = false;
    Serial.printf("STORE: flushed %lu records\n", (unsigned long)record_count);
}

String StorageManager::getHistoryJson() {
    String out;
    out.reserve(4096);
    out = "{\"records\":[";

    uint32_t start = (record_count < MAX_RECORDS) ? 0 : ring_head;
    uint32_t skip  = (record_count > 50) ? record_count - 50 : 0;
    bool first = true;

    for (uint32_t i = skip; i < record_count; ++i) {
        uint32_t idx = (start + i) % MAX_RECORDS;
        const HistoryRecord &r = ring_buffer[idx];
        char e[140];
        snprintf(e, sizeof(e),
            "%s{\"t\":%lu,\"ph\":%.2f,\"ec\":%d,\"tw\":%.1f,"
            "\"ta\":%.1f,\"ha\":%.1f,\"l\":%u,\"h\":%u}",
            first ? "" : ",",
            (unsigned long)r.timestamp_ms, r.ph, r.tds,
            r.temp_water, r.temp_air, r.hum_air, r.level, r.health);
        out += e;
        first = false;
    }

    out += "],\"total\":";
    out += String(record_count);
    out += '}';
    return out;
}
