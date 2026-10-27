#include "BluettiConfig.h"
#include "BTooth.h"
#include "utils.h"
#include "PayloadParser.h"
#include "BWifi.h"
#include "display.h"
#include "config.h"
#include "RestartDiagnostics.h"
#include <Preferences.h>
#include <string.h>


int pollTick = 0;

struct command_handle {
  uint8_t page;
  uint8_t offset;
  int length;
};

QueueHandle_t commandHandleQueue;
QueueHandle_t sendQueue;

// All HA control writes (MQTT and HTTP) use one serialized queue and are
// retried when the Bluetti does not acknowledge the Modbus write.
struct queued_control_command {
  bt_command_t command;
  uint8_t retries;
};
static QueueHandle_t controlCommandQueue;
static const uint8_t MAX_CONTROL_RETRIES = 2; // 3 attempts total
static bool blePendingIsControl = false;
static uint8_t bleControlRetries = 0;
static bt_command_t blePendingControlCommand;

unsigned long lastBTMessage = 0;

// Runtime-adjustable polling delay; defaults to the original 3000 ms.
static unsigned long bluetoothQueryDelayMs = BLUETOOTH_QUERY_MESSAGE_DELAY;

// Measurement-round metadata shared with HTTP/JSON and MQTT state.
static volatile bool measurementRoundActive = false;
static volatile uint8_t measurementRoundErrorCode = 1; // no complete successful round yet
static volatile uint32_t measurementRoundCompletedAtMs = 0;
static bool measurementRoundHadError = false;

bool isMeasurementRoundActive() { return measurementRoundActive; }
uint8_t getMeasurementRoundErrorCode() { return measurementRoundErrorCode; }
uint32_t getMeasurementRoundCompletedAtMs() { return measurementRoundCompletedAtMs; }


void initBluetoothQueryDelay() {
  Preferences prefs;
  if (prefs.begin("bluettibt", true)) {
    unsigned long stored = prefs.getULong("querydelay", BLUETOOTH_QUERY_MESSAGE_DELAY);
    prefs.end();
    if (stored >= 500 && stored <= 30000) bluetoothQueryDelayMs = stored;
  }
}

bool setBluetoothQueryDelay(unsigned long delayMs) {
  if (delayMs < 500 || delayMs > 30000) return false;
  Preferences prefs;
  if (!prefs.begin("bluettibt", false)) return false;
  size_t written = prefs.putULong("querydelay", delayMs);
  prefs.end();
  if (written == 0) return false;
  bluetoothQueryDelayMs = delayMs;
  return true;
}

unsigned long getBluetoothQueryDelay() {
  return bluetoothQueryDelayMs;
}

// BLE response reassembly buffer
static uint8_t bleResponseBuffer[260];
static size_t bleResponseLength = 0;
static size_t bleExpectedLength = 0;

static bool bleResponseActive = false;
static uint32_t bleLastRxTime = 0;
static bool bleCommandPending = false;
static uint32_t bleCommandSentAt = 0;
static uint32_t bleIgnoreRxUntil = 0;

static bt_command_t bleCurrentCommand;

// Diagnostic context for the last outstanding Bluetooth request.
static uint16_t blePendingPollIndex = 0;
static uint8_t blePendingPage = 0;
static uint8_t blePendingOffset = 0;
static uint16_t blePendingLength = 0;
static uint16_t bleRxFragmentCount = 0;
static uint16_t bleRxBytesForCommand = 0;
static uint32_t bleLastAbortAtMs = 0;

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
    Serial.println(F("BLE - onConnect"));
     #ifdef DISPLAYSSD1306
      disp_setBlueTooth(true);
     #endif
  }

  void onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println(F("BLE - onDisconnect"));
    #ifdef DISPLAYSSD1306
      disp_setBlueTooth(false);
    #endif
    #ifdef RELAISMODE
      #ifdef DEBUG
        Serial.println(F("deactivate relais contact"));
      #endif
      digitalWrite(RELAIS_PIN, RELAIS_LOW);
    #endif
  }
};

/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */
class BluettiAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
 /**
   * Called for each advertising BLE server.
   */
  void onResult(BLEAdvertisedDevice *advertisedDevice) {
    Serial.print(F("[BLE] Advertised Device found: "));
    Serial.println(advertisedDevice->toString().c_str());

     ESPBluettiSettings settings = get_esp32_bluetti_settings();
    // We have found a device, let us now see if it contains the service we are looking for.
    if (advertisedDevice->haveServiceUUID() && advertisedDevice->isAdvertisingService(serviceUUID) && (strcmp(advertisedDevice->getName().c_str(),settings.bluetti_device_id)==0) ) {
      BLEDevice::getScan()->stop();
      bluettiDevice = advertisedDevice;
      doConnect = true;
      doScan = true;
    }
  } 
};
//debugfunktion
void logBleRawData(const uint8_t* data, size_t length) {
  String hex = "";

  for (size_t i = 0; i < length; i++) {
    if (data[i] < 0x10) {
      hex += "0";
    }

    hex += String(data[i], HEX);
    hex += " ";
  }

  hex.toUpperCase();

  AddtoMsgView(
    String(millis()) +
    ": BLE RX [" +
    String(length) +
    " bytes]: " +
    hex
  );
}

static void retryOrFinishControlCommand(const char *reason)
{
    const uint32_t now = millis();
    queued_control_command retry;
    retry.command = blePendingControlCommand;
    retry.retries = bleControlRetries;

    bleResponseActive = false;
    bleResponseLength = 0;
    bleExpectedLength = 0;
    xQueueReset(commandHandleQueue);
    bleCommandPending = false;
    measurementRoundActive = false;
    measurementRoundHadError = true;
    measurementRoundErrorCode = 2;
    pollTick = 0;
    bleIgnoreRxUntil = now + BLUETOOTH_SCAN_TIMEOUT;
    lastBTMessage = now;

    String message = String("BLE control command failed: ") + reason +
        " page=0x" + String(blePendingControlCommand.page, HEX) +
        " offset=0x" + String(blePendingControlCommand.offset, HEX) +
        " attempt=" + String(bleControlRetries + 1) + "/" + String(MAX_CONTROL_RETRIES + 1);

    if (bleControlRetries < MAX_CONTROL_RETRIES && controlCommandQueue != nullptr) {
        retry.retries++;
        if (xQueueSendToFront(controlCommandQueue, &retry, 0) == pdTRUE) {
            message += "; retry queued";
        } else {
            message += "; retry queue full, command dropped";
        }
    } else {
        message += "; retry limit reached, command abandoned";
    }
    AddtoMsgView(message);
    Serial.println(message);

    blePendingIsControl = false;
    bleControlRetries = 0;
    bleRxFragmentCount = 0;
    bleRxBytesForCommand = 0;
}

static void abortBleResponse()
{
    if (blePendingIsControl) {
        retryOrFinishControlCommand("invalid or incomplete response");
        return;
    }
    uint32_t now = millis();

    bleResponseActive = false;
    bleResponseLength = 0;
    bleExpectedLength = 0;

    // Nur die Queue für die Antwortzuordnung zurücksetzen.
    xQueueReset(commandHandleQueue);

    bleCommandPending = false;
    measurementRoundActive = false;
    measurementRoundHadError = true;
    measurementRoundErrorCode = 2; // Bluetooth response timeout/invalid frame

    // A failed response invalidates the whole measurement round. Do not continue
    // with the next block index: restart from the first block after the quiet
    // period, otherwise later blocks can be polled while the round is already
    // marked failed/inactive.
    pollTick = 0;

    bleIgnoreRxUntil = now + BLUETOOTH_SCAN_TIMEOUT;
    lastBTMessage = now;

    String reason = "BLE abort: block=" + String(blePendingPollIndex) +
        " page=0x" + String(blePendingPage, HEX) +
        " offset=0x" + String(blePendingOffset, HEX) +
        " len=" + String(blePendingLength) +
        " waited_ms=" + String((uint32_t)(now - bleCommandSentAt)) +
        " rx_fragments=" + String(bleRxFragmentCount) +
        " rx_bytes=" + String(bleRxBytesForCommand) +
        " response_active=" + String(bleResponseActive ? 1 : 0) +
        " response_bytes=" + String(bleResponseLength) +
        " expected_bytes=" + String(bleExpectedLength);
    AddtoMsgView(reason);
    bleLastAbortAtMs = now;
    bleRxFragmentCount = 0;
    bleRxBytesForCommand = 0;
}


void handleBluettiNotification(
    uint8_t* pData,
    size_t length
) {
    if (pData == nullptr || length == 0) {
        return;
    }
    // Während der Sperrzeit keine verspäteten Antworten übernehmen
    if (bleIgnoreRxUntil != 0 &&
        (int32_t)(millis() - bleIgnoreRxUntil) < 0) {
        return;
    }

    bleIgnoreRxUntil = 0;
    const uint32_t now = millis();

    if (bleCommandPending) {
        bleRxFragmentCount++;
        bleRxBytesForCommand += (uint16_t)length;
    }

    // Veraltete, unvollständige Antwort verwerfen
    if (bleResponseActive &&
        (uint32_t)(now - bleLastRxTime) > BLUETOOTH_SCAN_TIMEOUT) {

        /*AddtoMsgView(
            "BLE RX: incomplete response discarded, bytes: " +
            String(bleResponseLength)
        );*/

        abortBleResponse();
        return;
    }

    // Noch keine Antwort aktiv: Anfang eines Modbus-Rahmens suchen
    if (!bleResponseActive) {

        // Normale Read-Holding-Registers-Antwort
        bool normalResponse =
            length >= 3 &&
            pData[0] == 0x01 &&
            pData[1] == 0x03;

        // Modbus-Exception-Antwort
        bool exceptionResponse =
            length >= 3 &&
            pData[0] == 0x01 &&
            (pData[1] == 0x83 || pData[1] == 0x86);
        bool writeAckResponse =
            length >= 3 && pData[0] == 0x01 && pData[1] == 0x06;

        if (!normalResponse && !exceptionResponse && !writeAckResponse) {
            // Kein erkennbarer Antwortanfang.
            // Keine Polling-Anfrage aus der Queue entnehmen.
            return;
        }

        // Die zugehörige Anfrage genau einmal übernehmen
        if (xQueueReceive(
                commandHandleQueue,
                &bleCurrentCommand,
                0
            ) != pdTRUE) {

            /*AddtoMsgView(
                "BLE RX: response without queued command"
            );*/
            return;
        }

        bleResponseActive = true;
        bleResponseLength = 0;

        if (exceptionResponse) {
            // Slave-ID + Funktion + Fehlercode + CRC
            bleExpectedLength = 5;
        } else if (writeAckResponse) {
            // Modbus write-single-register echo: request bytes + CRC
            bleExpectedLength = 8;
        } else {
            // Slave-ID + Funktion + Bytecount + Daten + CRC
            bleExpectedLength = (size_t)pData[2] + 5;
        }

        if (bleExpectedLength > sizeof(bleResponseBuffer)) {
            //AddtoMsgView("BLE RX: response length exceeds buffer");

            abortBleResponse();
            return;
        }
    }

    bleLastRxTime = now;

    // Überlauf verhindern
    if (length > sizeof(bleResponseBuffer) - bleResponseLength) {
        //AddtoMsgView("BLE RX: response buffer overflow");

        abortBleResponse();
		return;
    }

    // Aktuelles Fragment anhängen
    memcpy(
        bleResponseBuffer + bleResponseLength,
        pData,
        length
    );

    bleResponseLength += length;

    // Antwort noch nicht vollständig
    if (bleResponseLength < bleExpectedLength) {
        return;
    }

    // Zu viele Bytes: nicht ungeprüft an den Parser übergeben
    if (bleResponseLength != bleExpectedLength) {
        /*AddtoMsgView(
            "BLE RX: unexpected response size: " +
            String(bleResponseLength) +
            ", expected: " +
            String(bleExpectedLength)
        );*/

        abortBleResponse();
		return;
    }

    // Vollständigen Rahmen protokollieren
    String packetHex;

    for (size_t i = 0; i < bleResponseLength; i++) {
        if (bleResponseBuffer[i] < 0x10) {
            packetHex += "0";
        }

        packetHex += String(bleResponseBuffer[i], HEX);
        packetHex += " ";
    }

    packetHex.toUpperCase();

    /*AddtoMsgView(
        "BLE RX COMPLETE [" +
        String(bleResponseLength) +
        " bytes]: " +
        packetHex
    );*/

    // Control writes are acknowledged with Modbus function 0x06 (echo of the
    // request). Do not feed these acknowledgements into the measurement parser.
    if (blePendingIsControl) {
        if (bleResponseBuffer[1] == 0x06 &&
            bleResponseLength == 8 &&
            memcmp(bleResponseBuffer, &blePendingControlCommand, 6) == 0) {
            AddtoMsgView("BLE control command acknowledged page=0x" +
                String(blePendingControlCommand.page, HEX) + " offset=0x" +
                String(blePendingControlCommand.offset, HEX) +
                " attempt=" + String(bleControlRetries + 1));
            bleResponseActive = false;
            bleResponseLength = 0;
            bleExpectedLength = 0;
            bleCommandPending = false;
            blePendingIsControl = false;
            bleControlRetries = 0;
            bleRxFragmentCount = 0;
            bleRxBytesForCommand = 0;
            return;
        }
        retryOrFinishControlCommand(bleResponseBuffer[1] == 0x86 ?
            "device returned Modbus exception" : "unexpected write acknowledgement");
        return;
    }

    // Exception frames are complete frames but not valid measurement responses.
    if (bleResponseBuffer[1] == 0x83) {
        measurementRoundHadError = true;
        measurementRoundErrorCode = 3; // device returned Modbus exception
    }

    // Parser genau einmal pro vollständiger Antwort aufrufen, normal replies only.
    if (bleResponseBuffer[1] == 0x03) parse_bluetooth_data(
        bleCurrentCommand.page,
        bleCurrentCommand.offset,
        bleResponseBuffer,
        bleResponseLength
    );

    // Detect completion of the last configured register block.
    const size_t commandCount = sizeof(bluetti_polling_command) / sizeof(device_field_data_t);
    const device_field_data_t &lastCommand = bluetti_polling_command[commandCount - 1];
    if (bleCurrentCommand.page == lastCommand.f_page && bleCurrentCommand.offset == lastCommand.f_offset) {
        measurementRoundActive = false;
        if (!measurementRoundHadError && bleResponseBuffer[1] == 0x03) {
            measurementRoundErrorCode = 0;
            measurementRoundCompletedAtMs = millis();
        } else if (measurementRoundErrorCode == 0) {
            measurementRoundErrorCode = 4; // incomplete/invalid round
        }
    }

    // Zustand für die nächste Antwort zurücksetzen
    bleResponseActive = false;
    bleResponseLength = 0;
    bleExpectedLength = 0;
	bleCommandPending = false;
}


static void checkBleResponseTimeout()
{
    if (!bleCommandPending) {
        return;
    }

    const uint32_t now = millis();

    // Anfrage läuft länger als erlaubt
	if ((uint32_t)(now - bleCommandSentAt) > BLUETOOTH_SCAN_TIMEOUT) {
        const bool wasControlCommand = blePendingIsControl;
		abortBleResponse();
        if (!wasControlCommand) {
		    AddtoMsgView("BLE timeout: pending response discarded");
        }
	}
}


void initBluetooth(){
  BLEDevice::init("");
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new BluettiAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
  
  commandHandleQueue = xQueueCreate( 5, sizeof(bt_command_t ) );
  sendQueue = xQueueCreate( 5, sizeof(bt_command_t) );
  controlCommandQueue = xQueueCreate( 10, sizeof(queued_control_command) );
}


static void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {

#ifdef DEBUG
    Serial.println("[BT] F01 - Write Response");
    /* pData Debug... */
    for (int i=1; i<=length; i++){
       
      Serial.printf("%02x", pData[i-1]);
      
      if(i % 2 == 0){
        Serial.print(" "); 
      } 

      if(i % 16 == 0){
        Serial.println();  
      }
    }
    Serial.println();
#endif

    /*bt_command_t command_handle;
    if(xQueueReceive(commandHandleQueue, &command_handle, 500)){
      parse_bluetooth_data(command_handle.page, command_handle.offset, pData, length);
    }
   logBleRawData(pData, length);*/
   handleBluettiNotification(pData, length);
   //logBleRawData(pData, length);
}

bool connectToServer() {
    Serial.print(F("[BT] Forming a connection to "));
    Serial.println(bluettiDevice->getAddress().toString().c_str());

    BLEDevice::setMTU(517); // set client to request maximum MTU from server (default is 23 otherwise)
    BLEClient*  pClient  = BLEDevice::createClient();
    Serial.println(F("[BT] - Created client"));

    pClient->setClientCallbacks(new MyClientCallback());

    // Connect to the remove BLE Server.
    pClient->connect(bluettiDevice);  // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
    Serial.println(F("[BT] - Connected to server"));
    // pClient->setMTU(517); //set client to request maximum MTU from server (default is 23 otherwise)
  
    // Obtain a reference to the service we are after in the remote BLE server.
    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
      Serial.print(F("[BT] Failed to find our service UUID: "));
      Serial.println(serviceUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(F("[BT] - Found our service"));


    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteWriteCharacteristic = pRemoteService->getCharacteristic(WRITE_UUID);
    if (pRemoteWriteCharacteristic == nullptr) {
      Serial.print(F("[BT] Failed to find our characteristic UUID: "));
      Serial.println(WRITE_UUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(F("[BT] - Found our Write characteristic"));

        // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteNotifyCharacteristic = pRemoteService->getCharacteristic(NOTIFY_UUID);
    if (pRemoteNotifyCharacteristic == nullptr) {
      Serial.print(F("[BT] Failed to find our characteristic UUID: "));
      Serial.println(NOTIFY_UUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(F("[BT] - Found our Write characteristic"));

    // Read the value of the characteristic.
    if(pRemoteWriteCharacteristic->canRead()) {
      std::string value = pRemoteWriteCharacteristic->readValue();
      Serial.print(F("[BT] The characteristic value was: "));
      Serial.println(value.c_str());
    }

    if(pRemoteNotifyCharacteristic->canNotify())
      pRemoteNotifyCharacteristic->registerForNotify(notifyCallback);

    connected = true;
     #ifdef RELAISMODE
      #ifdef DEBUG
        Serial.println(F("[BT] activate relais contact"));
      #endif
      digitalWrite(RELAIS_PIN, RELAIS_HIGH);
    #endif

    return true;
}


void handleBTCommandQueue(){

    bt_command_t command;
    if(xQueueReceive(sendQueue, &command, 0)) {
      
#ifdef DEBUG
    Serial.print("[BT] Write Request FF02 - Value: ");
    
    for(int i=0; i<8; i++){
       if ( i % 2 == 0){ Serial.print(" "); };
       Serial.printf("%02x", ((uint8_t*)&command)[i]);
    }
    
    Serial.println("");
#endif
      pRemoteWriteCharacteristic->writeValue((uint8_t*)&command, sizeof(command),true);
 
     };  
}

void sendBTCommand(bt_command_t command){
    if (controlCommandQueue == nullptr) {
        AddtoMsgView("BLE control command rejected: queue not initialized");
        return;
    }
    queued_control_command queued;
    queued.command = command;
    queued.retries = 0;
    if (xQueueSend(controlCommandQueue, &queued, 0) != pdTRUE) {
        AddtoMsgView("BLE control command rejected: control queue full");
        Serial.println(F("[BLE] Control command queue full; command rejected"));
    }
}

void handleBluetooth(){
  checkBleResponseTimeout();
  if (doConnect == true) {
    if (connectToServer()) {
      Serial.println(F("We are now connected to the Bluetti BLE Server."));
    } else {
      Serial.println(F("We have failed to connect to the server; there is nothing more we will do."));
    }
    doConnect = false;
  }

  if ((millis() - lastBTMessage) > (MAX_DISCONNECTED_TIME_UNTIL_REBOOT * 60000)){ 
    Serial.println(F("[BT] disconnected over allowed limit, reboot device"));
	AddtoMsgView("[BT] disconnected over allowed limit, reboot device");
    recordRestartCause("BT_DISCONNECTED_TIMEOUT");
	delay(1000);
    #ifdef SLEEP_TIME_ON_BT_NOT_AVAIL
        esp_deep_sleep_start();
    #else
        ESP.restart();
    #endif
  }

  if (connected) {

    // poll for device state
    uint32_t now = millis();

	bool bleQuietPeriodActive =
    bleIgnoreRxUntil != 0 &&
    (int32_t)(now - bleIgnoreRxUntil) < 0;

	if (bleIgnoreRxUntil != 0 && !bleQuietPeriodActive) {
     bleIgnoreRxUntil = 0;
	}

    // Prioritize control writes from MQTT/HTTP over measurement polling. Every
    // write waits for its response before any further BLE request is sent.
    if (!bleCommandPending && !bleQuietPeriodActive && controlCommandQueue != nullptr) {
      queued_control_command queuedControl;
      if (xQueueReceive(controlCommandQueue, &queuedControl, 0) == pdTRUE) {
        blePendingIsControl = true;
        blePendingControlCommand = queuedControl.command;
        bleControlRetries = queuedControl.retries;
        blePendingPage = queuedControl.command.page;
        blePendingOffset = queuedControl.command.offset;
        blePendingLength = 2;
        bleRxFragmentCount = 0;
        bleRxBytesForCommand = 0;
        bleCommandPending = true;
        bleCommandSentAt = millis();
        xQueueSend(commandHandleQueue, &queuedControl.command, 0);
        xQueueSend(sendQueue, &queuedControl.command, 0);
        lastBTMessage = millis();
        AddtoMsgView("BLE control command queued for send page=0x" +
            String(queuedControl.command.page, HEX) + " offset=0x" +
            String(queuedControl.command.offset, HEX) + " attempt=" +
            String(queuedControl.retries + 1));
      }
    }

	// The configured delay is a pause between complete rounds, not between blocks.
    // Continue immediately while pollTick points at another block in this round.
    if (!bleCommandPending && !bleQuietPeriodActive &&
        (pollTick != 0 || millis() - lastBTMessage > bluetoothQueryDelayMs)) {

       if (pollTick == 0) {
         measurementRoundActive = true;
         measurementRoundHadError = false;
         measurementRoundErrorCode = 1; // round in progress
       }
       bt_command_t command;
       command.prefix = 0x01;
       command.field_update_cmd = 0x03;
       command.page = bluetti_polling_command[pollTick].f_page;
       command.offset = bluetti_polling_command[pollTick].f_offset;
       command.len = (uint16_t) bluetti_polling_command[pollTick].f_size << 8;
       command.check_sum = modbus_crc((uint8_t*)&command,6);
       blePendingIsControl = false;
       blePendingPollIndex = (uint16_t)pollTick;
       blePendingPage = command.page;
       blePendingOffset = command.offset;
       blePendingLength = command.len >> 8;
       bleRxFragmentCount = 0;
       bleRxBytesForCommand = 0;
	   bleCommandPending = true; //Nachricht abgesendet und Sperre aktiv
	   bleCommandSentAt = millis(); //Zeitpunkt des Absendens > für Timeout
       xQueueSend(commandHandleQueue, &command, portMAX_DELAY);
       xQueueSend(sendQueue, &command, portMAX_DELAY);

       if (pollTick == sizeof(bluetti_polling_command)/sizeof(device_field_data_t)-1 ){
           pollTick = 0;
       } else {
           pollTick++;
       }
            
      lastBTMessage = millis();
    }

    handleBTCommandQueue();
    
  }else if(doScan){
    BLEDevice::getScan()->start(0);
  } 
}

void btResetStack()
{
  connected=false;

}

bool isBTconnected(){
  return connected;
}

// True only while a register request is awaiting its Bluetooth response.
bool isBluetoothTransferActive(){
  return bleCommandPending;
}

// Includes a lost BLE link and protocol/response errors from register polling.
bool isBluetoothError(){
  return !connected || measurementRoundErrorCode == 2 ||
      measurementRoundErrorCode == 3 || measurementRoundErrorCode == 4;
}

unsigned long getLastBTMessageTime(){
    return lastBTMessage;
}


