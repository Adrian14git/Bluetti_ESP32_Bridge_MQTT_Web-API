#ifndef BWIFI_H
#define BWIFI_H
#include "Arduino.h"
#include "config.h"
class AsyncWebServerRequest;

typedef struct{
  int  salt = EEPROM_SALT;
  char mqtt_server[40] = "127.0.0.1";
  char mqtt_port[6]  = "1883";
  char mqtt_username[40] = "";
  char mqtt_password[40] = "";
  char bluetti_device_id[40] = "Bluetti Blutetooth Id";
  char ota_username[40] = "";
  char ota_password[40] = "";
  // Optional HTTP Basic authentication for the local web UI and API.
  bool http_auth_enabled = false;
  char http_username[32] = "admin";
  char http_password[64] = "";
  uint32_t http_auth_config_magic = 0;
} ESPBluettiSettings;

extern ESPBluettiSettings get_esp32_bluetti_settings();
extern void initBWifi(bool resetWifi);
extern void handleWebserver();
extern bool isWifiConnected();
extern bool authenticateHttpRequest(AsyncWebServerRequest *request);
String processorWebsiteUpdates(const String& var);
extern void AddtoMsgView(String data);
  
#endif
