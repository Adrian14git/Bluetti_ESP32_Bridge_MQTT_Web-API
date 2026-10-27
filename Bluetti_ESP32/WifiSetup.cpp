#include "WifiSetup.h"
#include "BluettiConfig.h"
#include "display.h"
#include <WiFiManager.h>

bool setupWifiManager(bool resetWifi, ESPBluettiSettings &wifiConfig, bool &shouldSaveConfig) {
  char mqtt_server[40]; strlcpy(mqtt_server, wifiConfig.mqtt_server, sizeof(mqtt_server));
  char mqtt_port[6]; strlcpy(mqtt_port, wifiConfig.mqtt_port, sizeof(mqtt_port));
  char bluetti_device_id[40]; strlcpy(bluetti_device_id, wifiConfig.bluetti_device_id, sizeof(bluetti_device_id));

  WiFiManagerParameter custom_mqtt_server("server", "MQTT Server Address", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "MQTT Server Port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_username("username", "MQTT Username", wifiConfig.mqtt_username, 40);
  WiFiManagerParameter custom_mqtt_password("password", "MQTT Password", wifiConfig.mqtt_password, 40, "type=password");
  WiFiManagerParameter custom_ota_username("ota_username", "OTA Username", wifiConfig.ota_username, 40);
  WiFiManagerParameter custom_ota_password("ota_password", "OTA Password", wifiConfig.ota_password, 40, "type=password");
  WiFiManagerParameter custom_bluetti_device("bluetti", "Bluetti Bluetooth ID", bluetti_device_id, 40);

  WiFiManager wifiManager;
  if (resetWifi) wifiManager.resetSettings();
  else if (wifiConfig.salt != EEPROM_SALT) {
    Serial.println("Invalid settings in EEPROM, trying with defaults");
    ESPBluettiSettings defaults;
    wifiConfig = defaults;
  } else wifiManager.setConfigPortalTimeout(300);

  wifiManager.setSaveConfigCallback([&shouldSaveConfig]() { shouldSaveConfig = true; });
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_username);
  wifiManager.addParameter(&custom_mqtt_password);
  wifiManager.addParameter(&custom_ota_username);
  wifiManager.addParameter(&custom_ota_password);
  wifiManager.addParameter(&custom_bluetti_device);
  wifiManager.setAPCallback([](WiFiManager *manager) {
    Serial.printf("Entered config mode:ip=%s, ssid='%s'\n", WiFi.softAPIP().toString().c_str(), manager->getConfigPortalSSID().c_str());
#ifdef DISPLAYSSD1306
    wrDisp_wifisignal(2);
    wrDisp_IP(WiFi.softAPIP().toString().c_str());
    wrDisp_Status("Setup Wifi");
#endif
  });

  if (!wifiManager.autoConnect("Bluetti_ESP32")) return false;
  if (shouldSaveConfig) {
    strlcpy(wifiConfig.mqtt_server, custom_mqtt_server.getValue(), sizeof(wifiConfig.mqtt_server));
    strlcpy(wifiConfig.mqtt_port, custom_mqtt_port.getValue(), sizeof(wifiConfig.mqtt_port));
    strlcpy(wifiConfig.mqtt_username, custom_mqtt_username.getValue(), sizeof(wifiConfig.mqtt_username));
    strlcpy(wifiConfig.mqtt_password, custom_mqtt_password.getValue(), sizeof(wifiConfig.mqtt_password));
    strlcpy(wifiConfig.ota_username, custom_ota_username.getValue(), sizeof(wifiConfig.ota_username));
    strlcpy(wifiConfig.ota_password, custom_ota_password.getValue(), sizeof(wifiConfig.ota_password));
    strlcpy(wifiConfig.bluetti_device_id, custom_bluetti_device.getValue(), sizeof(wifiConfig.bluetti_device_id));
  }
  return true;
}
