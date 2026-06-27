#include <Arduino.h>
#include "hal_setup.h"
#include "assets_fs.h"
#include "comms/uart_comm.h"
#include "data_model.h"
#include "storage/settings_store.h"
#include "ui/ui_manager.h"
#include "automation/automation_engine.h"
#include "network/network_manager.h"
#include "storage/storage_manager.h"

static TaskHandle_t hTaskUI;
static TaskHandle_t hTaskUART;
static TaskHandle_t hTaskAuto;
static TaskHandle_t hTaskNet;
static TaskHandle_t hTaskStore;

static void heartbeat() {
    static uint32_t prev = 0;
    uint32_t now = millis();
    if (now - prev < 5000) return;
    prev = now;

    DataModel::getInstance().lock();
    DataModel::getInstance().getState().uptime_seconds = now / 1000;
    DataModel::getInstance().unlock();

    Serial.printf("[HB] up=%lus heap=%u\n",
                  (unsigned long)(now / 1000), (unsigned)ESP.getFreeHeap());
}

// ── Core 1: LVGL display ────────────────────────────────────────

static void taskUI(void *) {
    for (;;) {
        hal_loop();
        UiManager::getInstance().update();
        heartbeat();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ── Core 0: UART to Arduino controller ──────────────────────────

static void taskUART(void *) {
    for (;;) {
        UartComm::getInstance().process();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ── Core 1: Automation engine ───────────────────────────────────

static void taskAutomation(void *) {
    vTaskDelay(pdMS_TO_TICKS(3000));
    for (;;) {
        AutomationEngine::getInstance().evaluate();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// ── Core 0: WiFi + HTTP server ──────────────────────────────────

static void taskNetwork(void *) {
    vTaskDelay(pdMS_TO_TICKS(2000));
    NetworkManager::getInstance().begin();
    for (;;) {
        NetworkManager::getInstance().loop();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ── Core 1: Data logging ────────────────────────────────────────

static void taskStorage(void *) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    StorageManager::getInstance().begin();
    for (;;) {
        StorageManager::getInstance().loop();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ── Entry point ─────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n========================================");
    Serial.println("  e-Weed HMI v3.0");
    Serial.println("  Hydroponic Automation Platform");
    Serial.println("========================================\n");

    DataModel::getInstance().lock();
    settingsLoadInto(DataModel::getInstance().getSettings());
    SystemSettings uart_cfg = DataModel::getInstance().getSettings();
    DataModel::getInstance().unlock();
    UartComm::getInstance().begin(static_cast<long>(uart_cfg.uart_baud), uart_cfg.uart_rx_pin,
                                  uart_cfg.uart_tx_pin);

    Serial.println("SETUP: hal_setup()");
    hal_setup();

    Serial.println("SETUP: assets_fs_init()");
    assets_fs_init();
    assets_fs_log_startup_report();

    Serial.println("SETUP: UiManager::init()");
    UiManager::getInstance().init();

    Serial.println("SETUP: Creating FreeRTOS tasks...");
    xTaskCreatePinnedToCore(taskUI,         "UI",    16384, nullptr, 2, &hTaskUI,    1);
    xTaskCreatePinnedToCore(taskUART,       "UART",   4096, nullptr, 1, &hTaskUART,  0);
    xTaskCreatePinnedToCore(taskAutomation, "Auto",   3072, nullptr, 1, &hTaskAuto,  1);
    xTaskCreatePinnedToCore(taskNetwork,    "Net",    8192, nullptr, 1, &hTaskNet,   0);
    xTaskCreatePinnedToCore(taskStorage,    "Store",  4096, nullptr, 1, &hTaskStore, 1);

    Serial.printf("SETUP: done  heap=%u\n", (unsigned)ESP.getFreeHeap());
}

void loop() {
    vTaskDelete(nullptr);
}
