Bluetti ESP32 Bridge - Neustartdiagnose
=======================================

Änderungen in dieser Version:
- Feste Diagnosekarte auf der Weboberfläche.
- Letzte bekannte Software-Neustartursache wird in ESP32-NVS (Preferences) gespeichert.
- Zusätzlich wird esp_reset_reason() beim Start ausgewertet.
- Erfasste Softwareursachen:
  * BT_DISCONNECTED_TIMEOUT
  * MQTT_TIMEOUT
  * MQTT_INVALID_DEVICE_TYPE
  * WIFI_MANAGER_CONNECT_FAILED
  * MANUAL_WEB_REBOOT
- "NO_SOFTWARE_CAUSE_RECORDED" bedeutet, dass keiner der instrumentierten
  Neustartpfade die Ursache vor dem Reset gespeichert hat. Dann ist der
  angezeigte ESP32-Resetgrund (z.B. WATCHDOG oder BROWNOUT) der nächste Hinweis.
- "Laufzeit vor Neustart" ist millis() beim Auslösen eines instrumentierten
  Neustarts. Bei unerwarteten Hardware-/Watchdog-Resets ist sie "unavailable".

Installation:
1. Bestehendes Projekt sichern.
2. Diesen Projektordner in Arduino IDE öffnen.
3. Mit derselben ESP32-Boardversion und Bibliotheken wie bisher kompilieren.
4. Erst nach erfolgreichem Build flashen.

Die Diagnosekarte zeigt den letzten Reset; sie wird nicht zeitgesteuert gelöscht.
Ein Neustart über die Weboberfläche wird als MANUAL_WEB_REBOOT protokolliert.

MQTT configuration: Bluetooth query delay
-----------------------------------------
Set the polling delay by publishing an integer number of milliseconds to:
  bluetti/<device_id>/command/bluetooth_query_delay
Allowed values are 500 through 30000 ms. The default is 3000 ms.
The value is saved in ESP32 Preferences and survives restarts.
The current value is published retained to:
  bluetti/<device_id>/state/bluetooth_query_delay
