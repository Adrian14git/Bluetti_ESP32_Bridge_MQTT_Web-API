#include "BluettiConfig.h"
#include "BWifi.h"
#include "BTooth.h"
#include "MQTT.h"
#include "index.h"  //Web page header file
#include <EEPROM.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h> // https://github.com/me-no-dev/AsyncTCP/archive/master.zip
#include <ESPmDNS.h>
#include <esp_wifi.h>
#include <ElegantOTA.h> // https://github.com/ayushsharma82/ElegantOTA/archive/master.zip
#include "display.h"
#include "RestartDiagnostics.h"
#include "utils.h"
#include "WifiSetup.h"

AsyncWebServer server(80);
AsyncEventSource events("/events");

unsigned long lastTimeWebUpdate = 0;  

String lastMsg = ""; 

bool msgViewerDetails = false;
bool shouldSaveConfig = false;
int wifiReconnectCounter = 0;

char mqtt_server[40] = "127.0.0.1";
char mqtt_port[6]  = "1883";
char bluetti_device_id[40] = "EP5002204000015656";


ESPBluettiSettings wifiConfig;
void eeprom_saveconfig();
static const uint32_t HTTP_AUTH_CONFIG_MAGIC = 0x48545450; // "HTTP"

ESPBluettiSettings get_esp32_bluetti_settings(){
    return wifiConfig;
    return wifiConfig;
}

void eeprom_read(){
  Serial.println(F("Loading Values from EEPROM"));
  EEPROM.begin(512);
  EEPROM.get(0, wifiConfig);
  EEPROM.end();
  // Migrate old EEPROM layouts safely: preserve existing Wi-Fi/MQTT settings,
  // initialize only the newly introduced HTTP-auth fields.
  if (wifiConfig.http_auth_config_magic != HTTP_AUTH_CONFIG_MAGIC) {
    wifiConfig.http_auth_enabled = false;
    strlcpy(wifiConfig.http_username, "admin", sizeof(wifiConfig.http_username));
    wifiConfig.http_password[0] = '\0';
    wifiConfig.http_auth_config_magic = HTTP_AUTH_CONFIG_MAGIC;
    eeprom_saveconfig();
  }
}

void eeprom_saveconfig(){
  Serial.println(F("Saving Values to EEPROM"));
  EEPROM.begin(512);
  EEPROM.put(0, wifiConfig);
  EEPROM.commit();
  EEPROM.end();
}

void setWiFiPowerSavingMode(){
  //esp_wifi_set_ps(WIFI_PS_MAX_MODEM); // maximum power saving, does not make sense here
  //esp_wifi_set_ps(WIFI_PS_NONE); // will cause kernel panic and reboot on my ESP32 (AlexBurghardt)
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM); // default
}


static String extractJsonField(const String &json, const String &key) {
  String needle = "\"" + key + "\"";
  int pos = json.indexOf(needle);
  if (pos < 0) return String();
  pos = json.indexOf(':', pos + needle.length());
  if (pos < 0) return String();
  pos++;
  while (pos < (int)json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) pos++;
  if (pos >= (int)json.length()) return String();
  if (json[pos] == '"') {
    int start = ++pos;
    while (pos < (int)json.length()) {
      if (json[pos] == '"' && (pos == start || json[pos-1] != '\\')) return json.substring(start, pos);
      pos++;
    }
    return String();
  }
  int start = pos;
  while (pos < (int)json.length() && json[pos] != ',' && json[pos] != '}' && json[pos] != ' ' && json[pos] != '\r' && json[pos] != '\n') pos++;
  return json.substring(start, pos);
}
static void collectJsonBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  if (total > 2048) return;
  if (index == 0) {
    char *body = new char[total + 1];
    body[0] = '\0';
    request->_tempObject = body;
  }
  char *body = static_cast<char *>(request->_tempObject);
  if (!body) return;
  memcpy(body + index, data, len);
  body[index + len] = '\0';
}
static String requestValue(AsyncWebServerRequest *request, const String &body, const char *key) {
  if (request->hasParam(key, true)) return request->getParam(key, true)->value();
  return extractJsonField(body, key);
}

bool authenticateHttpRequest(AsyncWebServerRequest *request) {
  if (!wifiConfig.http_auth_enabled) return true;
  if (request->authenticate(wifiConfig.http_username, wifiConfig.http_password)) return true;
  request->requestAuthentication();
  return false;
}

static String jsonEscape(const String &input) {
  String out; out.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); ++i) {
    char c = input[i];
    if (c == '\\' || c == '"') { out += '\\'; out += c; }
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if ((uint8_t)c >= 0x20) out += c;
  }
  return out;
}

void initBWifi(bool resetWifi){

  eeprom_read();

  if (resetWifi) {
    ESPBluettiSettings defaults;
    wifiConfig = defaults;
    wifiConfig.http_auth_config_magic = HTTP_AUTH_CONFIG_MAGIC;
    eeprom_saveconfig();
  }
  if (!setupWifiManager(resetWifi, wifiConfig, shouldSaveConfig)) {
    recordRestartCause("WIFI_MANAGER_CONNECT_FAILED");
    ESP.restart();
    return;
  }
  if (shouldSaveConfig) eeprom_saveconfig();

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    // display will have blinking wifi signal until connected.
    #ifdef DISPLAYSSD1306
      disp_setPrevStateIcon(0);
      wrDisp_wifisignal(0);
      delay(200);
      Serial.print(".");
      disp_setPrevStateIcon(1);
      wrDisp_wifisignal(0);
    #else
      delay(500);
      Serial.print(".");
    #endif

  }
  
  WiFi.setAutoReconnect(true);

  Serial.println(F(""));
  Serial.println(F("IP address: "));
  Serial.println(WiFi.localIP());
  #ifdef DISPLAYSSD1306
    wrDisp_IP(WiFi.localIP().toString().c_str());
    disp_setWifiSignal(1, WiFi.RSSI());
  #endif
  if (MDNS.begin(DEVICE_NAME)) {
    Serial.println(F("MDNS responder started"));
  }

  //setup web server handling
  #if MSG_VIEWER_DETAILS
      msgViewerDetails = true;
      Serial.println(F("webserver BT/MQTT variable logging enabled..."));
    #else
      msgViewerDetails = false;
      Serial.println(F("webserver BT/MQTT variable logging disabled..."));
  #endif

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      if (!authenticateHttpRequest(request)) return;
      request->send_P(200, "text/html", index_html, processorWebsiteUpdates);
  });
  server.on("/switchLogging", HTTP_GET, [](AsyncWebServerRequest *request){
      if (!authenticateHttpRequest(request)) return;
      msgViewerDetails = !msgViewerDetails;
      if(msgViewerDetails){
        Serial.println(F("webserver BT/MQTT variable logging enabled..."));
      }
      else{
        Serial.println(F("webserver BT/MQTT variable logging disabled..."));
      }
      request->send_P(200, "text/html", index_html, processorWebsiteUpdates);
  });
  server.on("/toggleMQTT", HTTP_GET, [](AsyncWebServerRequest *request) {
      if (!authenticateHttpRequest(request)) return;
      setMQTTEnabled(!isMQTTEnabled());
      request->redirect("/");
  });
  server.on("/rebootDevice", HTTP_GET, [](AsyncWebServerRequest *request) {
      if (!authenticateHttpRequest(request)) return;
      request->send(200, "text/plain", "reboot in 2sec");
      delay(2000);
      recordRestartCause("MANUAL_WEB_REBOOT");
      ESP.restart();
  });
  server.on("/resetConfig", HTTP_GET, [](AsyncWebServerRequest *request) {
      if (!authenticateHttpRequest(request)) return;
      request->send(200, "text/plain", "reset Wifi and reboot in 2sec");
      delay(2000);
      initBWifi(true);
  });
  // Local JSON API. MQTT can be toggled independently at runtime.
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!authenticateHttpRequest(request)) return;
    String json = "{\"ip\":\"" + WiFi.localIP().toString() + "\",\"mac\":\"" + WiFi.macAddress() +
      "\",\"ssid\":\"" + jsonEscape(WiFi.SSID()) + "\",\"rssi\":" + String(WiFi.RSSI()) +
      ",\"uptime_ms\":" + String(millis()) + ",\"mqtt_enabled\":" + String(isMQTTEnabled() ? "true" : "false") + ",\"mqtt_connected\":" + String(isMQTTconnected() ? "true" : "false") +
      ",\"bluetooth_connected\":" + String(isBTconnected() ? "true" : "false") +
      ",\"mqtt_last_message_ms\":" + String(getLastMQTTMessageTime()) +
      ",\"bluetooth_last_message_ms\":" + String(getLastBTMessageTime()) +
      ",\"publish_errors\":" + String(getPublishErrorCount()) +
      ",\"last_restart_cause\":\"" + jsonEscape(getLastRestartCause()) +
      "\",\"last_reset_reason\":\"" + jsonEscape(getLastResetReason()) +
      "\",\"last_restart_uptime_ms\":\"" + jsonEscape(getLastRestartUptime()) + "\"}";
    request->send(200, "application/json", json);
  });
  server.on("/api/measurements", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!authenticateHttpRequest(request)) return;
    request->send(200, "application/json", getMqttMeasurementsJson());
  });
  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!authenticateHttpRequest(request)) return;
    String json = "{\"device_id\":\"" + jsonEscape(String(wifiConfig.bluetti_device_id)) +
      "\",\"bluetooth_query_delay_ms\":" + String(getBluetoothQueryDelay()) +
      ",\"http_auth_enabled\":" + String(wifiConfig.http_auth_enabled ? "true" : "false") +
      ",\"http_username\":\"" + jsonEscape(String(wifiConfig.http_username)) + "\"}";
    request->send(200, "application/json", json);
  });
  server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!authenticateHttpRequest(request)) return;
    String body = request->_tempObject ? String(static_cast<char *>(request->_tempObject)) : String();
    if (request->_tempObject) { delete[] static_cast<char *>(request->_tempObject); request->_tempObject = nullptr; }
    bool changed = false;
    String delayValue = requestValue(request, body, "bluetooth_query_delay_ms");
    if (delayValue.length()) {
      long v = delayValue.toInt();
      if (v < 500 || v > 30000 || !setBluetoothQueryDelay((unsigned long)v)) {
        request->send(400, "application/json", "{\"error\":\"bluetooth_query_delay_ms must be 500..30000\"}"); return;
      }
      changed = true;
    }
    String authValue = requestValue(request, body, "http_auth_enabled");
    if (authValue.length()) {
      String v = authValue;
      bool enable = (v == "true" || v == "1" || v == "on");
      if (enable) {
        String username = requestValue(request, body, "http_username");
        String password = requestValue(request, body, "http_password");
        if (!username.length()) username = String(wifiConfig.http_username);
        if (!password.length()) password = String(wifiConfig.http_password);
        if (username.length() == 0 || password.length() < 8 || username.length() >= sizeof(wifiConfig.http_username) || password.length() >= sizeof(wifiConfig.http_password)) {
          request->send(400, "application/json", "{\"error\":\"auth requires username and password (8+ chars)\"}"); return;
        }
        strlcpy(wifiConfig.http_username, username.c_str(), sizeof(wifiConfig.http_username));
        strlcpy(wifiConfig.http_password, password.c_str(), sizeof(wifiConfig.http_password));
      }
      wifiConfig.http_auth_enabled = enable;
      changed = true;
    } else {
      String postedUsername = requestValue(request, body, "http_username");
      if (postedUsername.length()) {
        String username = postedUsername;
        if (username.length() == 0 || username.length() >= sizeof(wifiConfig.http_username)) { request->send(400, "application/json", "{\"error\":\"invalid username\"}"); return; }
        strlcpy(wifiConfig.http_username, username.c_str(), sizeof(wifiConfig.http_username)); changed = true;
      }
      String postedPassword = requestValue(request, body, "http_password");
      if (postedPassword.length()) {
        String password = postedPassword;
        if (password.length() > 0 && (password.length() < 8 || password.length() >= sizeof(wifiConfig.http_password))) { request->send(400, "application/json", "{\"error\":\"password must be 8..63 characters\"}"); return; }
        if (password.length() > 0) { strlcpy(wifiConfig.http_password, password.c_str(), sizeof(wifiConfig.http_password)); changed = true; }
      }
    }
    if (changed) eeprom_saveconfig();
    request->send(200, "application/json", "{\"ok\":true,\"restart_required\":false}");
  }, nullptr, collectJsonBody);
  server.on("/api/command", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!authenticateHttpRequest(request)) return;
    String body = request->_tempObject ? String(static_cast<char *>(request->_tempObject)) : String();
    if (request->_tempObject) { delete[] static_cast<char *>(request->_tempObject); request->_tempObject = nullptr; }
    String commandName = requestValue(request, body, "name");
    String commandValue = requestValue(request, body, "value");
    if (!commandName.length() || !commandValue.length()) {
      request->send(400, "application/json", "{\"error\":\"name and value required\"}"); return;
    }
    String name = commandName; name.toLowerCase();
    String value = commandValue;
    if (name == "bluetooth_query_delay") {
      long v = value.toInt();
      if (v < 500 || v > 30000 || !setBluetoothQueryDelay((unsigned long)v)) { request->send(400, "application/json", "{\"error\":\"value must be 500..30000\"}"); return; }
      publishBluetoothQueryDelayState();
      request->send(200, "application/json", "{\"ok\":true,\"bluetooth_query_delay_ms\":" + String(getBluetoothQueryDelay()) + "}"); return;
    }
    bool found = false;
    bt_command_t command; command.prefix = 0x01; command.field_update_cmd = 0x06;
    for (int i=0; i<sizeof(bluetti_device_command)/sizeof(device_field_data_t); i++) {
      String field = map_field_name(bluetti_device_command[i].f_name);
      if (name == field) { command.page = bluetti_device_command[i].f_page; command.offset = bluetti_device_command[i].f_offset; value = map_command_value(field, value); found = true; break; }
    }
    if (!found) { request->send(404, "application/json", "{\"error\":\"unknown command\"}"); return; }
    command.len = swap_bytes(value.toInt()); command.check_sum = modbus_crc((uint8_t*)&command, 6); sendBTCommand(command);
    request->send(202, "application/json", "{\"ok\":true,\"queued\":true}");
  }, nullptr, collectJsonBody);

  // Simple settings page. When auth is enabled, browser Basic Auth protects it.
  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!authenticateHttpRequest(request)) return;
    String page = F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>Bluetti Bridge Settings</title></head><body><h1>Bluetti Bridge API settings</h1><p>Lokale HTTP/JSON API. MQTT bleibt unverändert aktiv.</p><form method='post' action='/api/config'><label>Bluetooth-Abfrageintervall (500–30000 ms): <input name='bluetooth_query_delay_ms' type='number' min='500' max='30000' value='");
    page += String(getBluetoothQueryDelay());
    page += F("'></label><hr><label>HTTP-Authentifizierung: <select name='http_auth_enabled'><option value='false'");
    if (!wifiConfig.http_auth_enabled) page += F(" selected");
    page += F(">deaktiviert</option><option value='true'");
    if (wifiConfig.http_auth_enabled) page += F(" selected");
    page += F(">aktiviert</option></select></label><p>Benutzername: <input name='http_username' maxlength='31' value='");
    page += jsonEscape(String(wifiConfig.http_username));
    page += F("'></p><p>Neues Passwort (mind. 8 Zeichen; leer = unverändert): <input name='http_password' type='password' minlength='8' maxlength='63'></p><button type='submit'>Speichern</button></form><p>API: <a href='/api/status'>Status</a> | <a href='/api/measurements'>Messwerte</a> | <a href='/api/config'>Konfiguration</a></p><p><a href='/'>Zurück</a></p></body></html>");
    request->send(200, "text/html; charset=utf-8", page);
  });

  //setup web server events
  events.onConnect([](AsyncEventSourceClient *client){
    if(client->lastId()){
      Serial.printf("Client reconnected! Last message ID that it got is: %u\n", client->lastId());
    }
    client->send("hello my friend, I'm just your data feed!", NULL, millis(), 10000);
  });
  events.setFilter([](AsyncWebServerRequest *request) { return authenticateHttpRequest(request); });
  server.addHandler(&events);

  if (wifiConfig.http_auth_enabled) {
    ElegantOTA.begin(&server, wifiConfig.http_username, wifiConfig.http_password);
  } else if (strlen(wifiConfig.ota_username) > 0) {
    ElegantOTA.begin(&server, wifiConfig.ota_username, wifiConfig.ota_password);
  } else {
    ElegantOTA.begin(&server);
  }

  server.begin();
  Serial.println(F("HTTP server started"));

}

void handleWebserver() {
  
  //Serial.println(F("DEBUG handleWebserver"));
  if ((millis() - lastTimeWebUpdate) > MSG_VIEWER_REFRESH_CYCLE*1000) {
    
    // check wifi status every MSG_VIEWER_REFRESH_CYCLE and set display 
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(F("WiFi is disconnected, try to reconnect..."));
      #ifdef DISPLAYSSD1306
        disp_setWifiMode(0);
        disp_setStatus("Wifi err..");
      #endif
      WiFi.disconnect();
      WiFi.reconnect();
      AddtoMsgView(String(millis()) + ": WLAN ERROR! try to reconnect");
      wifiReconnectCounter++;
      //delay(1000); no delay as we only check every 5 seconds. Removing 1 second blocking of the program in the loop.
    } else {
      #ifdef DISPLAYSSD1306
        disp_setWifiSignal(1,WiFi.RSSI());
        if (wifiReconnectCounter > 0)
        {
          //only update display ones after wifi is recovered.
          disp_setStatus("Running!");
          wifiReconnectCounter = 0;
        }
      #endif
    }

    #ifdef DISPLAYSSD1306
      // update display
      disp_setBlueTooth(isBTconnected());
      disp_setMqttStatus(isMQTTconnected());
    #endif

    // Send Events to the Web Server with current data
    events.send("ping",NULL,millis());
    events.send(String(millis()).c_str(),"runtime",millis());
    events.send(String(WiFi.RSSI()).c_str(),"rssi",millis());
    events.send(String(isMQTTconnected()).c_str(),"mqtt_connected",millis());
    events.send(String(isMQTTEnabled()).c_str(),"mqtt_enabled",millis());
    events.send(String(getLastMQTTMessageTime()).c_str(),"mqtt_last_msg_time",millis());
    events.send(String(isBTconnected()).c_str(),"bt_connected",millis());
    events.send(String(getLastBTMessageTime()).c_str(),"bt_last_msg_time",millis());
    if(msgViewerDetails){
      events.send(lastMsg.c_str(),"last_msg",millis());
    } 
    
    lastTimeWebUpdate = millis();
  }
}


String processorWebsiteUpdates(const String& var){
  
  if(var == "IP"){
    return String(WiFi.localIP().toString());
  }
  else if(var == "RSSI"){
    return String(WiFi.RSSI());
  }
  else if(var == "SSID"){
    return String(WiFi.SSID());
  }
  else if(var == "MAC"){
    return String(WiFi.macAddress());
  }
  else if(var == "RUNTIME"){
    return String(millis());
  }
  else if(var == "MQTT_IP"){
    char msg[40];
    if (strlen(wifiConfig.mqtt_server) == 0){
      strlcpy(msg, "No MQTT server configured", 40);
    }else{
      strlcpy(msg, wifiConfig.mqtt_server, 40);
    }
    
    return msg;
  }
  else if(var == "MQTT_PORT"){
    char msg[6];
    strlcpy(msg, wifiConfig.mqtt_port, 6);
    return msg;
  }
  else if(var == "MQTT_CONNECTED"){
    return String(isMQTTconnected());
  }
  else if(var == "MQTT_ENABLED"){
    return String(isMQTTEnabled());
  }
  else if(var == "LAST_MQTT_MSG_TIME"){
    return String(getLastMQTTMessageTime());
  }
  else if(var == "DEVICE_ID"){
    char msg[40];
    strlcpy(msg, wifiConfig.bluetti_device_id, 40);
    return msg;
  }
  else if(var == "BT_CONNECTED"){
    return String(isBTconnected());
  }
  else if(var == "LAST_BT_MSG_TIME"){
    return String(getLastBTMessageTime());
  }
  else if(var == "BT_ERROR"){
    return String(getPublishErrorCount());
  }
  else if(var == "LAST_RESTART_CAUSE"){
    return getLastRestartCause();
  }
  else if(var == "LAST_RESET_REASON"){
    return getLastResetReason();
  }
  else if(var == "LAST_RESTART_UPTIME"){
    return getLastRestartUptime();
  }
  else if(var == "LAST_MSG"){
    if (msgViewerDetails){
      return String("...waiting for data...");
    }
    else{
      return String("...disabled...");
    }
  }
  else //return something, else this if then else will crash in case calles without VAR set....
  {
    return String("");
  }
}

void AddtoMsgView(String data){
  
  String tempMsg = "";
  
  int firstPos = lastMsg.indexOf("</p>");
  int nextPos = firstPos;
  int numEntry = 0;
  while(nextPos > 0){
    nextPos = lastMsg.indexOf("</p>",nextPos+4);
    if (nextPos > 0){
      numEntry++;
    }
  }

  if (numEntry > MSG_VIEWER_ENTRY_COUNT-2){
    tempMsg = lastMsg.substring(firstPos+4);
    lastMsg = tempMsg + "<p>" + data + "</p>";
  }
  else{
    lastMsg = lastMsg + "<p>" + data + "</p>";
  }
}

// Expose Wi-Fi link state to the status LED controller.
bool isWifiConnected() {
  return WiFi.status() == WL_CONNECTED;
}
