Bluetti ESP32 Bridge – Home Assistant Paket v5
=============================================

Dateien
-------
- bluetti_bridge.yaml: vorhandenes Paket, erweitert um EP500-Steuerungen.

Neue Entitäten
--------------
Schalter:
- Bluetti Netzladen (grid_charge_on)
- Bluetti Zeitsteuerung (time_control_on)
- Bluetti Split-Phase (split_phase_on)

Auswahllisten:
- Bluetti Betriebsmodus (ups_mode)
  CUSTOMIZED, PV_PRIORITY, STANDARD, TIME_CONTROL
- Bluetti Display-Ruhemodus (auto_sleep_mode)
  THIRTY_SECONDS, ONE_MINUTE, FIVE_MINUTES, NEVER
- Bluetti Split-Phase-Geräterolle (split_phase_machine_mode)
  SLAVE, MASTER

Zahlenregler:
- Bluetti Batteriegrenze unten (battery_range_start)
- Bluetti Batteriegrenze oben (battery_range_end)
  Bereich in dieser HA-Konfiguration: 0–100 %, Schritt 1.

Transportweg
------------
Alle neuen Steuerelemente verwenden input_select.bluetti_bridge_datenquelle:
- MQTT: bluetti/<Geräte-ID>/command/<feldname>
- HTTP: rest_command.bluetti_bridge_command_http

Die Auswahl bleibt manuell; es gibt keinen automatischen Fallback.

Voraussetzungen
---------------
- Die MQTT-Integration von Home Assistant ist eingerichtet.
- bluetti_bridge_api_url und bluetti_bridge_command_url sind in secrets.yaml definiert.
- Die ESP32-Firmware ist die EP500-Steuerungs-Version, die diese Felder im
  Geräteprofil und in der Befehlsverarbeitung unterstützt.
- Die Geräte-ID in input_text.bluetti_id entspricht der Bridge-Konfiguration.

Installation
------------
1. Bestehendes Paket sichern.
2. Diese bluetti_bridge.yaml anstelle der bisherigen Paketdatei einsetzen.
3. Home Assistant: Einstellungen -> Entwicklerwerkzeuge -> YAML -> Konfiguration prüfen.
4. Home Assistant neu starten.

Hinweise
--------
- Die neue YAML wurde syntaktisch mit einem YAML-Parser geprüft. Ein Test in einer
  laufenden Home-Assistant-Instanz war nicht möglich.
- Batteriegrenzen werden als 0–100 % mit Schrittweite 1 dargestellt. Bitte vor
  dem ersten Schreibvorgang sicherstellen, dass diese Grenzen zur konkreten
  EP500-Firmware passen.
- Die HTTP-Annahme eines Befehls beweist nicht, dass die Powerstation ihn
  bereits ausgeführt hat. Den resultierenden Status nach der nächsten
  erfolgreichen Bluetooth-Abfrage kontrollieren.
