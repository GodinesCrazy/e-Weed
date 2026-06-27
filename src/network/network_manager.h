#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include "data_model.h"
#include <WiFi.h>
#include <WebServer.h>

class NetworkManager {
public:
    static NetworkManager &getInstance() {
        static NetworkManager instance;
        return instance;
    }

    void begin();
    void loop();

private:
    NetworkManager() : server(80) {}

    WebServer server;
    bool      started    = false;
    bool      ap_active  = false;
    uint32_t  last_reconnect_ms = 0;

    void setupAP();
    void connectSTA();
    void setupRoutes();
    void updateState();

    void handleStatus();
    void handleSensors();
    void handleActuators();
    void handleActuatorsPost();
    void handleHistory();
    void handleConfig();
    void handleConfigPost();
    void handleNotFound();

    void cors();
    void json(int code, const char *body);
};

#endif // NETWORK_MANAGER_H
