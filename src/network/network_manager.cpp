#include "network/network_manager.h"
#include "storage/storage_manager.h"
#include "comms/uart_comm.h"

// ── WiFi lifecycle ──────────────────────────────────────────────

void NetworkManager::begin() {
    if (started) return;

    DataModel::getInstance().lock();
    WiFiConfig cfg = DataModel::getInstance().getSettings().wifi;
    DataModel::getInstance().unlock();

    if (!cfg.enabled) {
        Serial.println("NET: WiFi disabled");
        return;
    }

    if (strlen(cfg.ssid) > 0) {
        connectSTA();
        uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < cfg.sta_timeout_ms) {
            delay(250);
            Serial.print('.');
        }
        Serial.println();
        if (WiFi.status() != WL_CONNECTED && cfg.ap_fallback) {
            Serial.println("NET: STA failed → AP fallback");
            WiFi.disconnect();
            setupAP();
        }
    } else {
        setupAP();
    }

    setupRoutes();
    server.begin();
    started = true;
    updateState();
    Serial.printf("NET: HTTP server on port 80  IP=%s\n",
                  ap_active ? WiFi.softAPIP().toString().c_str()
                            : WiFi.localIP().toString().c_str());
}

void NetworkManager::setupAP() {
    DataModel::getInstance().lock();
    WiFiConfig cfg = DataModel::getInstance().getSettings().wifi;
    DataModel::getInstance().unlock();

    WiFi.mode(WIFI_AP);
    WiFi.softAP(cfg.ap_ssid, cfg.ap_password);
    ap_active = true;
    Serial.printf("NET: AP '%s' started\n", cfg.ap_ssid);
}

void NetworkManager::connectSTA() {
    DataModel::getInstance().lock();
    WiFiConfig cfg = DataModel::getInstance().getSettings().wifi;
    DataModel::getInstance().unlock();

    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.ssid, cfg.password);
    ap_active = false;
    Serial.printf("NET: Connecting to '%s'\n", cfg.ssid);
}

void NetworkManager::loop() {
    if (!started) return;

    server.handleClient();

    if (!ap_active && WiFi.status() != WL_CONNECTED) {
        uint32_t now = millis();
        if (now - last_reconnect_ms > 10000) {
            last_reconnect_ms = now;
            WiFi.reconnect();
        }
    }

    static uint32_t last_upd = 0;
    if (millis() - last_upd > 5000) {
        last_upd = millis();
        updateState();
    }
}

void NetworkManager::updateState() {
    DataModel::getInstance().lock();
    SystemState &s = DataModel::getInstance().getState();
    if (ap_active) {
        s.wifi_connected = true;
        s.wifi_ap_mode   = true;
        strncpy(s.wifi_ip, WiFi.softAPIP().toString().c_str(), sizeof(s.wifi_ip) - 1);
        s.wifi_rssi = 0;
    } else {
        s.wifi_connected = (WiFi.status() == WL_CONNECTED);
        s.wifi_ap_mode   = false;
        if (s.wifi_connected) {
            strncpy(s.wifi_ip, WiFi.localIP().toString().c_str(), sizeof(s.wifi_ip) - 1);
            s.wifi_rssi = WiFi.RSSI();
        } else {
            s.wifi_ip[0] = '\0';
            s.wifi_rssi  = 0;
        }
    }
    DataModel::getInstance().unlock();
}

// ── CORS / JSON helpers ─────────────────────────────────────────

void NetworkManager::cors() {
    server.sendHeader("Access-Control-Allow-Origin",  "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void NetworkManager::json(int code, const char *body) {
    cors();
    server.send(code, "application/json", body);
}

// ── Route registration ──────────────────────────────────────────

void NetworkManager::setupRoutes() {
    server.on("/api/status",    HTTP_GET,  [this]() { handleStatus();        });
    server.on("/api/sensors",   HTTP_GET,  [this]() { handleSensors();       });
    server.on("/api/actuators", HTTP_GET,  [this]() { handleActuators();     });
    server.on("/api/actuators", HTTP_POST, [this]() { handleActuatorsPost(); });
    server.on("/api/history",   HTTP_GET,  [this]() { handleHistory();       });
    server.on("/api/config",    HTTP_GET,  [this]() { handleConfig();        });
    server.on("/api/config",    HTTP_POST, [this]() { handleConfigPost();    });

    auto preflight = [this]() { cors(); server.send(204); };
    server.on("/api/status",    HTTP_OPTIONS, preflight);
    server.on("/api/sensors",   HTTP_OPTIONS, preflight);
    server.on("/api/actuators", HTTP_OPTIONS, preflight);
    server.on("/api/history",   HTTP_OPTIONS, preflight);
    server.on("/api/config",    HTTP_OPTIONS, preflight);

    server.onNotFound([this]() { handleNotFound(); });
}

// ── GET /api/status ─────────────────────────────────────────────

void NetworkManager::handleStatus() {
    DataModel::getInstance().lock();
    SystemState st = DataModel::getInstance().getState();
    DataModel::getInstance().unlock();

    char buf[640];
    snprintf(buf, sizeof(buf),
        "{\"ph\":%.2f,\"ec\":%d,\"temp_water\":%.1f,\"temp_water_probe\":%.1f,"
        "\"temp_air\":%.1f,"
        "\"humidity\":%.1f,\"level_min\":%s,\"level_max\":%s,"
        "\"ph_do\":%s,\"dht\":%s,\"ph_ok\":%s,\"tds_ok\":%s,\"tw_ok\":%s,"
        "\"health\":%d,\"stage\":%d,\"alarm\":%d,"
        "\"auto_mode\":%s,\"maintenance\":%s,"
        "\"uart\":%s,\"wifi_ip\":\"%s\","
        "\"uptime\":%lu,\"heap\":%u}",
        st.ph, st.tds, st.temp_water, st.temp_water_probe, st.temp_air, st.hum_air,
        st.level_min ? "true" : "false", st.level_max ? "true" : "false",
        st.ph_do_high ? "true" : "false",
        st.dht_online ? "true" : "false",
        st.ph_probe_ok ? "true" : "false",
        st.tds_probe_ok ? "true" : "false",
        st.tw_probe_ok ? "true" : "false",
        st.health, st.active_stage, st.current_alarm,
        st.auto_mode ? "true" : "false", st.maintenance_mode ? "true" : "false",
        st.uart_connected ? "true" : "false", st.wifi_ip,
        (unsigned long)(millis() / 1000), (unsigned)ESP.getFreeHeap());
    json(200, buf);
}

// ── GET /api/sensors ────────────────────────────────────────────

void NetworkManager::handleSensors() {
    DataModel::getInstance().lock();
    SystemState st = DataModel::getInstance().getState();
    DataModel::getInstance().unlock();

    char buf[448];
    snprintf(buf, sizeof(buf),
        "{\"ph\":%.2f,\"ec\":%d,\"temp_water\":%.1f,\"temp_water_probe\":%.1f,"
        "\"temp_air\":%.1f,\"humidity\":%.1f,"
        "\"level_min\":%s,\"level_max\":%s,\"ph_do\":%s,\"dht\":%s,"
        "\"ph_ok\":%s,\"tds_ok\":%s,\"tw_ok\":%s}",
        st.ph, st.tds, st.temp_water, st.temp_water_probe, st.temp_air, st.hum_air,
        st.level_min ? "true" : "false", st.level_max ? "true" : "false",
        st.ph_do_high ? "true" : "false",
        st.dht_online ? "true" : "false",
        st.ph_probe_ok ? "true" : "false",
        st.tds_probe_ok ? "true" : "false",
        st.tw_probe_ok ? "true" : "false");
    json(200, buf);
}

// ── GET /api/actuators ──────────────────────────────────────────

void NetworkManager::handleActuators() {
    DataModel::getInstance().lock();
    SystemState st = DataModel::getInstance().getState();
    DataModel::getInstance().unlock();

    char buf[384];
    snprintf(buf, sizeof(buf),
        "{\"light\":%s,\"intractor\":%s,\"extractor\":%s,"
        "\"recirculation\":%s,\"pump_in\":%s,"
        "\"pump_a\":%s,\"pump_b\":%s,"
        "\"ph_up\":%s,\"ph_down\":%s}",
        st.state_light ? "true" : "false",
        st.state_intractor ? "true" : "false",
        st.state_extractor ? "true" : "false",
        st.state_recirculation ? "true" : "false",
        st.state_pump_in ? "true" : "false",
        st.state_pump_a ? "true" : "false",
        st.state_pump_b ? "true" : "false",
        st.state_ph_up ? "true" : "false",
        st.state_ph_down ? "true" : "false");
    json(200, buf);
}

// ── POST /api/actuators  body: {"actuator":"LUZ","state":true} ──

void NetworkManager::handleActuatorsPost() {
    String body = server.arg("plain");
    int aIdx = body.indexOf("\"actuator\"");
    int sIdx = body.indexOf("\"state\"");
    if (aIdx < 0 || sIdx < 0) { json(400, "{\"error\":\"bad body\"}"); return; }

    int c1 = body.indexOf(':', aIdx);
    int q1 = body.indexOf('"', c1);
    int q2 = body.indexOf('"', q1 + 1);
    String key = body.substring(q1 + 1, q2);

    int c2 = body.indexOf(':', sIdx);
    String sv = body.substring(c2 + 1); sv.trim();
    bool val = sv.startsWith("true") || sv.startsWith("1");

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "CMD:OUT:%s:%d", key.c_str(), val ? 1 : 0);
    UartComm::getInstance().sendCommand(cmd);
    json(200, "{\"ok\":true}");
}

// ── GET /api/history ────────────────────────────────────────────

void NetworkManager::handleHistory() {
    String payload = StorageManager::getInstance().getHistoryJson();
    cors();
    server.send(200, "application/json", payload);
}

// ── GET /api/config ─────────────────────────────────────────────

void NetworkManager::handleConfig() {
    DataModel::getInstance().lock();
    AutomationConfig c = DataModel::getInstance().getSettings().automation;
    DataModel::getInstance().unlock();

    char buf[384];
    snprintf(buf, sizeof(buf),
        "{\"mode\":%d,\"ph_hysteresis\":%.2f,"
        "\"ec_hysteresis\":%d,\"temp_hysteresis\":%.1f,"
        "\"hum_hysteresis\":%.1f,"
        "\"min_dose_interval_ms\":%lu,"
        "\"dose_duration_ms\":%lu,"
        "\"eval_interval_ms\":%lu}",
        c.mode, c.ph_hysteresis, c.ec_hysteresis,
        c.temp_hysteresis, c.hum_hysteresis,
        (unsigned long)c.min_dose_interval_ms,
        (unsigned long)c.dose_duration_ms,
        (unsigned long)c.eval_interval_ms);
    json(200, buf);
}

// ── POST /api/config  body: {"mode":0..2} ───────────────────────

void NetworkManager::handleConfigPost() {
    String body = server.arg("plain");
    int mi = body.indexOf("\"mode\"");
    if (mi >= 0) {
        int c = body.indexOf(':', mi);
        int v = body.substring(c + 1).toInt();
        if (v >= 0 && v <= 2) {
            DataModel::getInstance().lock();
            DataModel::getInstance().getSettings().automation.mode = static_cast<AutoMode>(v);
            DataModel::getInstance().unlock();
        }
    }
    json(200, "{\"ok\":true}");
}

void NetworkManager::handleNotFound() {
    cors();
    server.send(404, "application/json", "{\"error\":\"not found\"}");
}
