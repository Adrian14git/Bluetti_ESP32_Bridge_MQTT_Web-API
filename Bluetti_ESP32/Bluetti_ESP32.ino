#include "BWifi.h"
#include "BTooth.h"
#include "MQTT.h"
#include "config.h"
#include "display.h"
#include "RestartDiagnostics.h"

// GPIO 8 status LED. This board drives the LED active-low (LOW = on, HIGH = off).
static const uint8_t STATUS_LED_PIN = 8;
static const uint8_t LED_ACTIVE_LEVEL = LOW;
static const uint8_t LED_INACTIVE_LEVEL = (LED_ACTIVE_LEVEL == HIGH) ? LOW : HIGH;
static unsigned long ledLastToggleMs = 0;
static bool ledIsOn = false;
static uint8_t ledMode = 0; // 0=off, 1=Bluetooth activity, 2=connection error

static void handleStatusLed() {
  // MQTT only counts as a fault when it is enabled; a deliberate web-page
  // disable is not an error. Wi-Fi and Bluetooth are required services.
  const bool connectionError = !isWifiConnected() || isBluetoothError() ||
      (isMQTTEnabled() && !isMQTTconnected());
  const bool btActivity = isBluetoothTransferActive();
  const uint8_t newMode = connectionError ? 2 : (btActivity ? 1 : 0);
  const unsigned long now = millis();

  if (newMode != ledMode) {
    ledMode = newMode;
    ledLastToggleMs = now;
    ledIsOn = false;
    digitalWrite(STATUS_LED_PIN, LED_INACTIVE_LEVEL);
  }

  if (ledMode == 0) {
    ledIsOn = false;
    digitalWrite(STATUS_LED_PIN, LED_INACTIVE_LEVEL);
    return;
  }

  const unsigned long intervalMs = (ledMode == 1) ? 50UL : 250UL;
  if (now - ledLastToggleMs >= intervalMs) {
    ledLastToggleMs = now;
    ledIsOn = !ledIsOn;
    digitalWrite(STATUS_LED_PIN, ledIsOn ? LED_ACTIVE_LEVEL : LED_INACTIVE_LEVEL);
  }
}

unsigned long lastTime1 = 0;
unsigned long timerDelay1 = 3000;

void setup() {
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LED_INACTIVE_LEVEL);
  Serial.begin(115200);
  initRestartDiagnostics();
  #ifdef RELAISMODE
    pinMode(RELAIS_PIN, OUTPUT);
    #ifdef DEBUG
      Serial.println(F("deactivate relais contact"));
    #endif
    digitalWrite(RELAIS_PIN, RELAIS_LOW);
  #endif
  #ifdef SLEEP_TIME_ON_BT_NOT_AVAIL
    esp_sleep_enable_timer_wakeup(SLEEP_TIME_ON_BT_NOT_AVAIL * 60 * 1000000ULL);
  #endif
  #ifdef DISPLAYSSD1306
    initDisplay();
  #endif
  initBWifi(false);
  initBluetoothQueryDelay();
  initBluetooth();
  initMQTT();
  #ifdef DISPLAYSSD1306
    wrDisp_Status("Running!");
  #endif
}

void loop() {
  #ifdef DISPLAYSSD1306
    handleDisplay();
  #endif
  handleBluetooth();
  handleMQTT(); 
  handleWebserver();
  handleStatusLed();
}
