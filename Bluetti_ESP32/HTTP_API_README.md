# Lokale HTTP/JSON-API

Die vorhandene MQTT-Schnittstelle bleibt erhalten. MQTT und HTTP verwenden dieselbe Bluetooth-Abfrageverzögerung und dieselbe Befehlslogik.

## Endpunkte

- `GET /api/status` – WLAN, Laufzeit, MQTT-/Bluetooth-Status und Neustartdiagnose.
- `GET /api/measurements` – zuletzt veröffentlichte Bluetti-Messwerte plus Verbindungsstatus und Abfrageintervall.
- `GET /api/config` – Geräte-ID, Bluetooth-Abfrageintervall und Authentifizierungsstatus (kein Passwort).
- `POST /api/config` – JSON-Konfiguration speichern.
- `POST /api/command` – einen vorhandenen Bluetti-Befehl ausführen.
- `GET /settings` – einfache Einstellungsseite für Intervall und optionale HTTP-Authentifizierung.

## Beispiele

Abfrage:

```sh
curl http://bluetti-bridge.local/api/status
curl http://bluetti-bridge.local/api/measurements
```

Abfrageintervall auf fünf Sekunden setzen:

```sh
curl -X POST http://bluetti-bridge.local/api/config \
  -H 'Content-Type: application/json' \
  -d '{"bluetooth_query_delay_ms":5000}'
```

Einen Befehl senden (der Name muss einem unterstützten MQTT-Befehlsfeld entsprechen):

```sh
curl -X POST http://bluetti-bridge.local/api/command \
  -H 'Content-Type: application/json' \
  -d '{"name":"ac_output_on","value":"ON"}'
```

Die Abfrageverzögerung muss zwischen 500 und 30000 ms liegen. Der Wert wird dauerhaft in Preferences gespeichert.

## Authentifizierung

Auf `/settings` kann HTTP Basic Authentication aktiviert oder deaktiviert werden. Bei Aktivierung sind Benutzername und ein Passwort mit mindestens acht Zeichen erforderlich. API-Routen, die Hauptseite, Aktionen und Server-Sent Events werden geschützt. ElegantOTA verwendet bei aktivierter HTTP-Authentifizierung dieselben Zugangsdaten.

**Sicherheitshinweis:** HTTP Basic Authentication verschlüsselt die Verbindung nicht. Zugangsdaten werden bei normalem HTTP nicht gegen Mitschneiden im Netzwerk geschützt. Die Bridge deshalb nicht per Portweiterleitung ins Internet freigeben; bei Bedarf nur über ein vertrauenswürdiges LAN oder einen abgesicherten VPN-Zugang verwenden.

## Projektstruktur / WiFiManager

`WifiSetup.cpp` kapselt den synchronen WiFiManager und bindet `WiFiManager.h` ein. `BWifi.cpp` bindet nur `ESPAsyncWebServer.h` ein. Dadurch werden die kollidierenden HTTP-Methoden-Enums nicht mehr im selben Übersetzungsmodul deklariert. Der vollständige Build muss in der lokalen Arduino-Umgebung mit den installierten Bibliotheksversionen verifiziert werden.
## MQTT runtime switch

The main web page includes an MQTT enable/disable control (`/toggleMQTT`). Disabling MQTT disconnects the MQTT client and stops MQTT reconnect/watchdog/publish processing; the HTTP API and Bluetooth polling continue to run. The switch is runtime-only and MQTT starts enabled again after a reboot. No MQTT “unavailable” or clearing payload is published when switching off, so Home Assistant can retain the last valid retained values until its configured timeout/availability logic marks them unavailable. Re-enable MQTT from the same web page to reconnect and resume publishing. The route uses the same HTTP authentication as the rest of the web UI.



## Automatische Wiederholung von Steuerbefehlen

Alle Steuerbefehle über MQTT und `POST /api/command` werden in der Firmware zentral serialisiert. Ein Steuerbefehl wartet, bis eine laufende Bluetooth-Abfrage abgeschlossen ist. Die Firmware erwartet für den Schreibbefehl eine Modbus-Bestätigung; bei Timeout, ungültiger Antwort oder Modbus-Ausnahme wird derselbe Sollwert erneut gesendet.

Es gibt maximal drei Versuche insgesamt (Erstversuch plus zwei Wiederholungen). Zwischen Versuchen wird die Bluetooth-Sperrzeit abgewartet. Wiederholungen haben Vorrang vor regulären Messwertabfragen. Nach drei erfolglosen Versuchen wird der Fehler in der ESP32-Diagnose protokolliert; der tatsächliche Live-Status der Powerstation muss weiterhin geprüft werden.

## EP500-Steuerbefehle (MQTT und HTTP)

Die folgenden Befehle sind in der EP500-Gerätedefinition als schreibbar hinterlegt. Die Werte werden über Bluetooth an die Powerstation übertragen; die erfolgreiche HTTP-Annahme bedeutet, dass der Befehl in die Übertragung eingereiht wurde, nicht dass die Powerstation ihn bereits bestätigt hat.

MQTT-Topic-Schema: `bluetti/<device_id>/command/<name>`. Zustände werden unter `bluetti/<device_id>/state/<name>` veröffentlicht.

| Name | Bedeutung | Zulässige Werte |
|---|---|---|
| `ac_output_on` | AC-Ausgang | `ON`, `OFF`, `1`, `0` |
| `dc_output_on` | DC-Ausgang | `ON`, `OFF`, `1`, `0` |
| `grid_charge_on` | Laden über Netzstrom | `ON`, `OFF`, `1`, `0` |
| `time_control_on` | Zeitsteuerung | `ON`, `OFF`, `1`, `0` |
| `split_phase_on` | Split-Phase aktivieren | `ON`, `OFF`, `1`, `0` |
| `split_phase_machine_mode` | Split-Phase-Geräterolle | `SLAVE` (0), `MASTER` (1) |
| `ups_mode` | USV-/Betriebsmodus | `CUSTOMIZED` (1), `PV_PRIORITY` (2), `STANDARD` (3), `TIME_CONTROL` (4) |
| `auto_sleep_mode` | Display-Ruhemodus | `THIRTY_SECONDS` (2), `ONE_MINUTE` (3), `FIVE_MINUTES` (4), `NEVER` (5) |
| `battery_range_start` | Untere Batteriegrenze | numerischer Registerwert |
| `battery_range_end` | Obere Batteriegrenze | numerischer Registerwert |

Die symbolischen Enum-Werte werden ohne Beachtung der Groß-/Kleinschreibung angenommen. Bei Zahlenwerten werden die Zahlen direkt an das Register geschrieben. Bitte nur Werte verwenden, die für das konkrete EP500-Modell und dessen Firmware gültig sind.

### HTTP-Beispiele

```sh
curl -X POST http://bluetti-bridge.local/api/command \
  -H 'Content-Type: application/json' \
  -d '{"name":"grid_charge_on","value":"ON"}'

curl -X POST http://bluetti-bridge.local/api/command \
  -H 'Content-Type: application/json' \
  -d '{"name":"ups_mode","value":"PV_PRIORITY"}'

curl -X POST http://bluetti-bridge.local/api/command \
  -H 'Content-Type: application/json' \
  -d '{"name":"battery_range_end","value":"100"}'
```

### MQTT-Beispiele

```text
Topic:   bluetti/<device_id>/command/grid_charge_on
Payload: ON

Topic:   bluetti/<device_id>/command/ups_mode
Payload: PV_PRIORITY

Topic:   bluetti/<device_id>/command/split_phase_machine_mode
Payload: MASTER
```

### Korrigierte AC-Messwerte

`internal_ac_voltage` wird jetzt mit einer Dezimalstelle und um den Faktor 10 korrigiert ausgegeben (z. B. `230.0` statt `23.00`). `internal_ac_frequency` wird ebenfalls um den Faktor 10 korrigiert (z. B. `50.0` statt `5.00`). Die Änderung betrifft diese beiden Messwerte im EP500-Profil.
