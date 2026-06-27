#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include "data_model.h"

class StorageManager {
public:
    static StorageManager &getInstance() {
        static StorageManager instance;
        return instance;
    }

    void     begin();
    void     loop();
    String   getHistoryJson();
    uint32_t getRecordCount() const { return record_count; }

private:
    StorageManager() {}

    static constexpr uint32_t    MAX_RECORDS  = 288; // 24 h @ 5 min
    static constexpr const char *HISTORY_FILE = "/history.csv";

    HistoryRecord ring_buffer[MAX_RECORDS];
    uint32_t ring_head     = 0;
    uint32_t record_count  = 0;
    uint32_t last_record_ms = 0;
    uint32_t last_flush_ms  = 0;
    bool     dirty          = false;

    void recordSample();
    void loadFromFS();
    void flushToFS();
    void addRecord(const HistoryRecord &r);
};

#endif // STORAGE_MANAGER_H
