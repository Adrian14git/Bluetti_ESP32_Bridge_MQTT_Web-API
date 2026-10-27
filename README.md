# Bluetti_ESP32_Bridge_MQTT_Web-API
based on https://github.com/mariolukas/Bluetti_ESP32_Bridge - Firmware mit MQTT-Abschaltung über die Weboberfläche, HTTP/JSON-API und Status-LED GPIO 8; dazu Home-Assistant-Paket v3 mit AC-/DC-Ausgangsschaltern

# Bluetti ESP32 Bridge – MQTT- und Web-API-Dokumentation

**Projektstand:** Firmware mit MQTT-Abschaltung über die Weboberfläche, HTTP/JSON-API und Status-LED GPIO 8; dazu Home-Assistant-Paket v3 mit AC-/DC-Ausgangsschaltern.

> **Wichtig zur Genauigkeit:** Die Topic-Struktur und HTTP-Endpunkte sind aus dem Quellstand der Firmware abgeleitet. Die Firmware ist für das konfigurierte Bluetti-Geräteprofil gebaut; die tatsächlich verfügbaren Messfelder und Schreibbefehle hängen vom verwendeten Profil in der Firmware ab. Beispiele verwenden die EP500-Geräte-ID `EP500xxxxxxxxxxx`; bitte durch die in der Bridge konfigurierte Geräte-ID ersetzen.

## 1. Architektur und Datenfluss

- Der ESP32 liest Messwerte von der Bluetti-Powerstation über Bluetooth.
- Messwerte werden als MQTT-Statusmeldungen veröffentlicht und zusätzlich über die lokale HTTP/JSON-API bereitgestellt.
- Home Assistant kann die Messwertquelle manuell über `input_select.bluetti_bridge_datenquelle` wählen: `MQTT` oder `HTTP`.
- Die AC-/DC-Ausgangsschalter senden Befehle ausschließlich über die ausgewählte Quelle. Es gibt **keinen automatischen Fallback** zwischen MQTT und HTTP.
- MQTT kann auf der ESP32-Weboberfläche vorübergehend abgeschaltet werden. HTTP und Bluetooth-Abfragen laufen dabei weiter. Nach einem Neustart ist MQTT standardmäßig wieder aktiviert.

## 2. MQTT-Schnittstelle

### 2.1 Topic-Schema

Die Firmware verwendet diese Topic-Struktur:

```text
bluetti/<device_id>/state/<field>
bluetti/<device_id>/command/<field>
```

`<device_id>` ist die in der Bridge konfigurierte Bluetti-Geräte-ID. Im aktuellen Firmware-Quellstand ist der Standardwert `EP500xxxxxxxxxxx`, sofern er nicht über die Konfiguration geändert wurde.

### 2.2 Status-Topics (`state`)

Messwerte werden unter `bluetti/<device_id>/state/<field>` veröffentlicht. Der Feldname ist kleingeschrieben, z. B.:

| Topic-Endung | Inhalt / Bedeutung |
|---|---|
| `ac_output_on` | Zustand AC-Ausgang |
| `dc_output_on` | Zustand DC-Ausgang |
| `ac_output_power` | AC-Ausgangsleistung |
| `dc_output_power` | DC-Ausgangsleistung |
| `ac_input_power` | AC-Eingangsleistung |
| `dc_input_power` | DC-Eingangsleistung |
| `ac_input_voltage` | AC-Eingangsspannung |
| `ac_input_frequency` | AC-Eingangsfrequenz |
| `power_generation` | Leistungs-/Erzeugungswert gemäß Geräteprofil |
| `total_battery_percent` | Gesamter Batterieladezustand |
| `pack_battery_percent` | Ladezustand eines Batterie-Packs |
| `pack_voltage` | Pack-Spannung |
| `internal_pack_voltage` | Interne Pack-Spannung |
| `serial_number` | Seriennummer |
| `device_type` | Gerätetyp |
| `arm_version`, `dsp_version` | Firmware-Versionen der Gerätekomponenten |
| `device` | JSON mit ESP32-IP, MAC-Adresse und Uptime |
| `device_status` | JSON mit MQTT- und Bluetooth-Verbindungsstatus |
| `bluetooth_query_delay` | Aktuelles Bluetooth-Abfrageintervall in Millisekunden |
| `error_code` | Statuscode der letzten Messrunde; `0` bedeutet fehlerfrei |
| `timestamp_ms` | Zeitpunkt der letzten vollständig erfolgreichen Messrunde als ESP32-Uptime in Millisekunden, **kein Unix-Zeitstempel** |
| `round_active` | `true`, wenn gerade eine Messrunde läuft |

**Vollständige Feldnamen des allgemeinen MQTT-Mappings** (nicht jedes Feld wird von jedem Geräteprofil geliefert):

```text
ac_output_power, dc_output_power, ac_output_on, dc_output_on, ac_output_mode,
power_generation, total_battery_percent, dc_input_power, ac_input_power,
ac_input_voltage, ac_input_frequency, pack_voltage, internal_pack_voltage,
serial_number, arm_version, dsp_version, device_type, ups_mode, auto_sleep_mode,
grid_charge_on, internal_ac_voltage, internal_ac_frequency,
internal_current_one, internal_power_one, internal_current_two, internal_power_two,
internal_current_three, internal_power_three, pack_max_num, pack_num,
pack_battery_percent, internal_dc_input_voltage, internal_dc_input_power,
internal_dc_input_current, internal_cell01_voltage ... internal_cell16_voltage,
led_mode, power_off, eco_on, eco_shutdown, charging_mode, power_lifting_on,
ac_input_power_max, ac_input_current_max, ac_output_power_max,
ac_output_current_max, battery_min_percentage, ac_charge_max_percentage,
battery_range_start, battery_range_end
```

Die tatsächliche Verfügbarkeit hängt vom ausgewählten Firmware-Geräteprofil und den darin definierten Polling-/Messfeldern ab. Das Mapping allein bedeutet nicht, dass jedes Feld vom aktiven Gerät publiziert wird oder als Befehl freigegeben ist.

**Payload-Formate:** Gewöhnliche Messwerte werden als Textwerte veröffentlicht. `device` und `device_status` sind JSON-Objekte als Payload. `error_code` und `timestamp_ms` sind numerische Textwerte; `round_active` ist `true`/`false`.

**Retain-Verhalten:** Die normalen Messwerte werden im aktuellen Code ohne Retain-Flag veröffentlicht. Die drei Diagnosewerte `error_code`, `timestamp_ms` und `round_active` sowie `bluetooth_query_delay` werden retained veröffentlicht. `device` und `device_status` werden ohne Retain veröffentlicht.

### 2.3 Befehls-Topics (`command`)

Befehle werden an `bluetti/<device_id>/command/<field>` gesendet. Die Firmware abonniert die Befehlsfelder aus `bluetti_device_command` des ausgewählten Geräteprofils. Für das aktuelle EP500-Profil sind dies:

| Topic-Endung | Zweck | Beispiel-Payload |
|---|---|---|
| `ac_output_on` | AC-Ausgang schalten | `ON` oder `OFF` |
| `dc_output_on` | DC-Ausgang schalten | `ON` oder `OFF` |
| `grid_charge_on` | Netzladen aktivieren/deaktivieren | `ON` oder `OFF` bzw. geräteabhängiger Zahlenwert |
| `ups_mode` | UPS-Modus setzen | numerischer Wert gemäß Bluetti-Protokoll |
| `pack_num` | Pack-Anzahl/-Auswahl setzen | numerischer Wert; nur mit Kenntnis des Geräteverhaltens verwenden |
| `auto_sleep_mode` | Display-Abschaltzeit/Energiesparparameter setzen | numerischer Wert gemäß Geräteprofil |
| `bluetooth_query_delay` | Bluetooth-Abfrageintervall ändern | Millisekunden, 500–30000 |

Für das EP500-Profil sind nur die tatsächlich im aktiven `bluetti_device_command[]` enthaltenen Felder als unterstützte Schreibbefehle zu betrachten. Weitere im Quellcode allgemein bekannte Felder (z. B. `led_mode`, `power_off`, `eco_on`, `charging_mode`) sind im EP500-Profil auskommentiert und daher dort nicht als aktiv unterstützte Befehle anzusehen.

**AC-/DC-Beispiele:**

```text
Topic:   bluetti/EP500xxxxxxxxxxx/command/ac_output_on
Payload: ON

Topic:   bluetti/EP500xxxxxxxxxxx/command/dc_output_on
Payload: OFF
```

Die Firmware wandelt `ON`/`OFF` für boolesche Schaltfelder in die numerischen Protokollwerte `1`/`0` um. Topic-Namen werden bei der Befehlsauswertung kleingeschrieben. Werte wie `ON` und `OFF` werden für die booleschen Felder ohne Beachtung der Groß-/Kleinschreibung verarbeitet.

**Intervall per MQTT ändern:**

```text
Topic:   bluetti/EP500xxxxxxxxxxx/command/bluetooth_query_delay
Payload: 5000
```

Erlaubt sind Werte von **500 bis 30000 Millisekunden**. Nach erfolgreicher Änderung veröffentlicht die Bridge den Wert auf `bluetti/<device_id>/state/bluetooth_query_delay`.

### 2.4 Automatische Wiederholung von Steuerbefehlen

Alle Steuerbefehle aus Home Assistant werden in der Firmware – unabhängig davon, ob sie über MQTT oder die HTTP-API eintreffen – zunächst in eine gemeinsame Warteschlange gelegt. Steuerbefehle werden nacheinander und nicht parallel zu einer laufenden Bluetooth-Registerabfrage gesendet.

- Die Firmware wartet auf die Modbus-Schreibbestätigung des Bluetti-Geräts.
- Bleibt die Bestätigung aus, kommt eine ungültige Antwort oder meldet das Gerät eine Modbus-Ausnahme, wird der gleiche Sollwert erneut gesendet.
- Es gibt maximal **drei Sendeversuche insgesamt** (Erstversuch plus zwei Wiederholungen). Zwischen fehlgeschlagenem Versuch und erneutem Senden wird die vorhandene Bluetooth-Sperrzeit abgewartet.
- Wiederholungen werden vor normalen Messwertabfragen abgearbeitet. Auch ein während einer laufenden Messwertabfrage eingehender Steuerbefehl bleibt in der Warteschlange.
- Nach Ausschöpfen der Versuche wird der Fehler in der ESP32-Diagnose protokolliert. Das bedeutet nicht automatisch, dass der gewünschte Gerätezustand erreicht wurde; der Live-Status bleibt maßgeblich.

Die Wiederholung gilt für sämtliche im aktiven `bluetti_device_command[]` des Geräteprofils enthaltenen Steuerfelder. Die Auswahl `MQTT`/`HTTP` in Home Assistant bleibt manuell; es gibt keinen automatischen Wechsel des Transportwegs.

### 2.5 MQTT-Verbindung und Home Assistant Discovery

- Die MQTT-Verbindungsdaten (Broker, Port, optional Benutzername/Passwort) werden über die Bridge-Konfiguration eingerichtet.
- Nach erfolgreicher MQTT-Verbindung veröffentlicht die Firmware Home-Assistant-Konfigurationen über ihre Discovery-Funktion.
- Home Assistant Paket v3 lauscht zusätzlich auf `bluetti/+/state/#` und filtert die Meldungen anhand des Helfers `input_text.bluetti_id`.
- In den HA-Schaltern gilt: `MQTT` ausgewählt → Befehle über `.../command/ac_output_on` bzw. `.../command/dc_output_on`, Payload `ON`/`OFF`.
- Der MQTT-Client verwendet die PubSubClient-Bibliothek. Der Code setzt für gewöhnliche `publish()`-Aufrufe keinen expliziten QoS; damit gelten die Standardwerte der Bibliothek (typischerweise QoS 0). MQTT-Command-Nachrichten sind nicht als bestätigte Ausführung zu verstehen: die Annahme des Befehls durch die Bridge ist keine Garantie, dass die Powerstation den Zustand tatsächlich geändert hat.

### 2.6 MQTT auf der Weboberfläche abschalten

Auf der Hauptseite gibt es die Funktion **MQTT abschalten / MQTT einschalten** (`/toggleMQTT`). Beim Abschalten trennt die Firmware den MQTT-Client und unterbindet MQTT-Reconnect-, Watchdog- und Publish-Verarbeitung. Bluetooth-Abfragen und HTTP-API bleiben aktiv.

- Die Abschaltung wird nicht dauerhaft gespeichert.
- Nach einem ESP32-Neustart startet MQTT wieder aktiviert.
- Beim absichtlichen Abschalten werden keine `unavailable`- oder Lösch-Payloads gesendet. In Home Assistant können deshalb letzte Werte zunächst erhalten bleiben; Verfügbarkeit/Timeout muss in HA passend bewertet werden.
- Ein absichtlich deaktiviertes MQTT gilt nicht als MQTT-Verbindungsfehler.

## 3. Lokale Weboberfläche und HTTP/JSON-API

Der Webserver verwendet HTTP auf Port **80**. Erreichbar ist er über die IP-Adresse der Bridge oder – sofern mDNS im Netzwerk funktioniert – über den konfigurierten `.local`-Hostnamen.

### 3.1 Endpunkte im Überblick

| Methode | Pfad | Zweck |
|---|---|---|
| `GET` | `/` | Haupt-Weboberfläche mit Status, Diagnose und MQTT-Schalter |
| `GET` | `/events` | Server-Sent Events (SSE) für Live-Updates der Weboberfläche |
| `GET` | `/switchLogging` | Detailanzeige für BT-/MQTT-Meldungen umschalten |
| `GET` | `/toggleMQTT` | MQTT zur Laufzeit ein-/ausschalten; leitet danach zur Hauptseite um |
| `GET` | `/rebootDevice` | ESP32-Neustart auslösen |
| `GET` | `/resetConfig` | WLAN-Konfiguration zurücksetzen und Neustart/Neuinitialisierung auslösen |
| `GET` | `/api/status` | Status von WLAN, MQTT, Bluetooth und Neustartdiagnose als JSON |
| `GET` | `/api/measurements` | Letzter vollständiger Messdatensatz plus Messrundenstatus als JSON |
| `GET` | `/api/config` | Geräte-ID, Abfrageintervall und Authentifizierungsstatus abrufen |
| `POST` | `/api/config` | Abfrageintervall und HTTP-Authentifizierung konfigurieren |
| `POST` | `/api/command` | Bluetti-Befehl senden oder Abfrageintervall setzen |
| `GET` | `/settings` | Einstellungsseite für Abfrageintervall und HTTP-Authentifizierung |

Alle genannten Webrouten durchlaufen die HTTP-Authentifizierungsprüfung, sofern die Basic Authentication aktiviert ist. `/events` verwendet dieselbe Prüfung.

### 3.2 `GET /api/status`

Beispielaufruf:

```http
GET /api/status HTTP/1.1
Host: bluetti-bridge.local
```

Antwort (Beispielstruktur; Laufzeitwerte variieren):

```json
{
  "ip": "xxx.xxx.xxx.xxx",
  "mac": "DC:DC:DC:DC:DC:DC",
  "ssid": "WLAN-Name",
  "rssi": -55,
  "uptime_ms": 7052000,
  "mqtt_enabled": true,
  "mqtt_connected": true,
  "bluetooth_connected": true,
  "mqtt_last_message_ms": 7051000,
  "bluetooth_last_message_ms": 7050000,
  "publish_errors": 0,
  "last_restart_cause": "",
  "last_reset_reason": "",
  "last_restart_uptime_ms": ""
}
```

Felder:

- `ip`, `mac`, `ssid`, `rssi`: WLAN-/Netzwerkstatus.
- `uptime_ms`: Laufzeit seit ESP32-Start in Millisekunden.
- `mqtt_enabled`: MQTT ist durch den Webschalter freigegeben.
- `mqtt_connected`: tatsächlicher Status der MQTT-Verbindung.
- `bluetooth_connected`: Status der Bluetooth-Verbindung.
- `mqtt_last_message_ms`, `bluetooth_last_message_ms`: Zeitwerte aus der ESP32-Laufzeit, keine Uhrzeit und kein Unix-Zeitstempel.
- `publish_errors`: Zähler für fehlgeschlagene MQTT-Publish-Aufrufe seit dem Start.
- `last_restart_cause`, `last_reset_reason`, `last_restart_uptime_ms`: Diagnose zum letzten Neustart.

### 3.3 `GET /api/measurements`

Dieser Endpunkt gibt den zuletzt zwischengespeicherten vollständigen Messdatensatz zurück. Die Daten werden während einer Messrunde nicht als gültiger vollständiger Satz ausgegeben, wenn ein Fehler vorliegt, eine Runde läuft oder noch keine vollständige Runde abgeschlossen wurde.

Beispielstruktur bei gültigem Datensatz:

```json
{
  "error_code": 0,
  "timestamp_ms": 123456,
  "round_active": false,
  "data": {
    "total_battery_percent": "84",
    "ac_output_power": "120",
    "dc_output_on": "1",
    "bluetooth_query_delay_ms": 3000,
    "mqtt_connected": true,
    "bluetooth_connected": true
  }
}
```

Die Messfeldnamen und deren Werte hängen vom Bluetti-Geräteprofil ab. In `data` sind die eigentlichen Messwerte meist als Zeichenketten enthalten; Diagnosefelder sind boolesch bzw. numerisch. Wenn der Datensatz nicht als vollständig gültig gilt, ist `data` `null`. `timestamp_ms` ist die ESP32-Uptime beim Abschluss der letzten erfolgreichen Messrunde und **keine Kalenderzeit**.

### 3.4 `GET /api/config`

Beispielantwort:

```json
{
  "device_id": "EP500xxxxxxxxxxx",
  "bluetooth_query_delay_ms": 3000,
  "http_auth_enabled": false,
  "http_username": "admin"
}
```

Das Passwort wird nie zurückgegeben.

### 3.5 `POST /api/config`

Der Request-Body ist JSON (`Content-Type: application/json`). Unterstützte Felder:

| Feld | Typ / zulässige Werte | Bedeutung |
|---|---|---|
| `bluetooth_query_delay_ms` | Integer `500` bis `30000` | Bluetooth-Abfrageintervall in Millisekunden; wird dauerhaft gespeichert |
| `http_auth_enabled` | Boolean (`true`/`false`; alternativ `1`/`0` oder `on`) | HTTP Basic Authentication aktivieren/deaktivieren |
| `http_username` | nicht leer, maximal 31 Zeichen | Benutzername |
| `http_password` | 8–63 Zeichen, wenn neu gesetzt | Neues Passwort; bei leerem Feld bleibt das vorhandene Passwort unverändert |

Intervall setzen:

```bash
curl -X POST http://bluetti-bridge.local/api/config \
  -H 'Content-Type: application/json' \
  -d '{"bluetooth_query_delay_ms":5000}'
```

Antwort bei Erfolg:

```json
{"ok":true,"restart_required":false}
```

Ungültige Intervallwerte liefern HTTP `400` und eine JSON-Fehlermeldung. Für das Aktivieren der Authentifizierung müssen ein Benutzername und ein Passwort mit mindestens acht Zeichen vorhanden sein.

### 3.6 `POST /api/command`

Dieser Endpunkt nimmt JSON mit `name` und `value` an und stellt den entsprechenden Befehl zur Bluetooth-Übertragung an die Powerstation in die Warteschlange.

**AC-Ausgang einschalten:**

```bash
curl -X POST http://bluetti-bridge.local/api/command \
  -H 'Content-Type: application/json' \
  -d '{"name":"ac_output_on","value":"ON"}'
```

**DC-Ausgang ausschalten:**

```bash
curl -X POST http://bluetti-bridge.local/api/command \
  -H 'Content-Type: application/json' \
  -d '{"name":"dc_output_on","value":"OFF"}'
```

Erfolg bei einem angenommenen Geräteschaltbefehl:

```json
{"ok":true,"queued":true}
```

HTTP-Status `202 Accepted` bedeutet, dass der Befehl angenommen und zur Übertragung eingereiht wurde. Es ist keine Bestätigung, dass die Powerstation den Befehl bereits ausgeführt hat. Der tatsächliche Zustand ist anschließend über Messwerte/Status zu prüfen.

Spezialbefehl für das Abfrageintervall:

```bash
curl -X POST http://bluetti-bridge.local/api/command \
  -H 'Content-Type: application/json' \
  -d '{"name":"bluetooth_query_delay","value":"5000"}'
```

Bei Erfolg antwortet dieser mit HTTP `200` und beispielsweise:

```json
{"ok":true,"bluetooth_query_delay_ms":5000}
```

Fehlerantworten:

- HTTP `400`: `name` oder `value` fehlt bzw. ungültiges Intervall.
- HTTP `404`: unbekannter/ im aktiven Geräteprofil nicht unterstützter Befehlsname.
- Bei aktivierter Authentifizierung fordert der Server bei fehlenden oder falschen Zugangsdaten HTTP Basic Authentication an.

### 3.7 `GET /settings`

Die Einstellungsseite bietet das Bluetooth-Abfrageintervall, Aktivierung/Deaktivierung der HTTP-Authentifizierung sowie Benutzername und neues Passwort. Sie verlinkt außerdem `/api/status`, `/api/measurements` und `/api/config`.

### 3.8 `GET /events` (Server-Sent Events)

Die Weboberfläche verwendet SSE unter `/events`, um Statuswerte live zu aktualisieren. Unter anderem werden Ereignisse für MQTT-Server/Port, MQTT aktiviert/verbunden, letzte MQTT-Nachricht sowie weitere Webstatuswerte versendet. Das ist primär ein internes Live-Update-Protokoll der Weboberfläche, kein Ersatz für `/api/measurements`.

## 4. HTTP-Authentifizierung und Sicherheit

Die optionale HTTP Basic Authentication wird über `/settings` oder `POST /api/config` konfiguriert. Bei aktivierter Authentifizierung werden Weboberfläche und API-Routen geschützt. Benutzername und Passwort werden im Browser bzw. HTTP-Header übertragen.

**HTTP Basic Authentication verschlüsselt den Verkehr nicht.** Ohne TLS können Zugangsdaten und Daten im lokalen Netzwerk mitgeschnitten werden. Die Bridge nicht per Portweiterleitung direkt aus dem Internet zugänglich machen; bei Fernzugriff VPN oder eine andere abgesicherte Verbindung verwenden.

Beispiel mit curl und Authentifizierung:

```bash
curl -u 'BENUTZER:PASSWORT' http://bluetti-bridge.local/api/status
```

## 5. Home-Assistant-Integration v3

### 5.1 Konfiguration

In `secrets.yaml` werden vollständige URLs angegeben:

```yaml
bluetti_bridge_api_url: "http://192.xxx.xxx.xxx/api/measurements"
bluetti_bridge_command_url: "http://192.xxx.xxx.xxx/api/command"
```

IP-Adresse und URL müssen zur aktuellen Bridge-Konfiguration passen. Falls die HTTP Basic Authentication aktiviert ist, muss die HTTP-Befehlsaktion ebenfalls mit den Zugangsdaten konfiguriert werden.

Das Paket benötigt aktivierte HA-Packages, beispielsweise:

```yaml
homeassistant:
  packages: !include_dir_named packages
```

### 5.2 Auswahl der Datenquelle

```yaml
input_select:
  bluetti_bridge_datenquelle:
    name: Bluetti Bridge Datenquelle
    options:
      - MQTT
      - HTTP
    initial: MQTT
    icon: mdi:swap-horizontal
```

- `MQTT`: Messwerte werden aus MQTT-Statusmeldungen bezogen; Befehle gehen an MQTT-Command-Topics.
- `HTTP`: Messwerte werden über `/api/measurements` abgerufen; Befehle werden per `POST /api/command` gesendet.
- Die Auswahl ist manuell und explizit. Es gibt keinen automatischen Wechsel bei einem Fehler.

Die Geräte-ID wird im Helfer `input_text.bluetti_id` gepflegt. MQTT-Routing hört auf `bluetti/+/state/#`, verarbeitet aber nur Meldungen der ausgewählten Geräte-ID.

### 5.3 AC-/DC-Ausgangsschalter

Das Paket v3 stellt folgende Entitäten bereit:

- `switch.bluetti_ac_ausgang`
- `switch.bluetti_dc_ausgang`

Beim Einschalten wird `ON`, beim Ausschalten `OFF` gesendet. Der ausgewählte Transportweg entscheidet über MQTT oder HTTP. Die Schalterrückmeldung soll aus den Messwerten kommen; nach dem Senden ist zu berücksichtigen, dass der Bluetti-Befehl über Bluetooth erst verarbeitet werden muss.

### 5.4 Messwert-Gültigkeit

Das HA-Paket verwendet den Status der Messrunde, um nur vollständige und gültige Datensätze zu übernehmen: `error_code == 0`, `round_active == false` und ein vorhandener Zeitstempel. `timestamp_ms` ist eine ESP32-Uptime in Millisekunden und kein Unix-Zeitstempel. Beim absichtlichen MQTT-Abschalten werden keine Lösch-/Unavailable-Nachrichten veröffentlicht; in MQTT-basierter HA-Ansicht können daher letzte Messwerte stehen bleiben, bis eine eigene Verfügbarkeits- oder Timeout-Logik greift.

## 6. Fehlerdiagnose

| Symptom | Prüfen |
|---|---|
| Keine HTTP-Messwerte | IP-Adresse und `bluetti_bridge_api_url` einschließlich `/api/measurements`; `/api/status` im Browser öffnen |
| HTTP `401` / Auth-Abfrage | HTTP Basic Authentication und Benutzername/Passwort prüfen |
| MQTT-Werte fehlen | `mqtt_enabled`, `mqtt_connected`, Broker-Adresse/-Port, Geräte-ID und Topic `bluetti/<device_id>/state/#` prüfen |
| MQTT ist absichtlich ausgeschaltet | HTTP läuft weiter; MQTT wird nach Neustart wieder aktiviert |
| AC-/DC-Befehl wird nicht umgesetzt | Richtige Datenquelle ausgewählt? Geräte-ID/Topic bzw. Command-URL korrekt? Bluetooth verbunden? Danach den tatsächlichen Status erneut lesen |
| `/api/command` liefert `404` | Feldname ist nicht im aktiven `bluetti_device_command[]` des Geräteprofils enthalten |
| Messwerte sind `null` | Messrunde läuft, hat Fehler gemeldet oder es wurde noch keine vollständige erfolgreiche Runde abgeschlossen |
| Alte MQTT-Werte bleiben stehen | Erwartetes Verhalten möglich, wenn MQTT abgeschaltet wird; die Bridge publiziert beim Abschalten keine Lösch-Payloads |

## 7. Hinweise zur Interpretation

1. Die API ist eine lokale, gerätespezifische Schnittstelle und kein allgemeines Bluetti-Cloud-API.
2. Ein erfolgreicher HTTP-Status `200` oder `202` bestätigt die Bearbeitung/Annahme des API-Aufrufs, nicht zwingend die physische Ausführung des Powerstation-Befehls.
3. MQTT-Statuswerte werden je nach Feld unterschiedlich retained publiziert; nicht von Retain für alle Messwerte ausgehen.
4. Verfügbare Felder und zulässige Befehle richten sich nach dem tatsächlich kompilierten Geräteprofil. Vor Nutzung schreibender Befehle außer AC/DC sollten die zulässigen Werte des konkreten Modells geprüft werden.
5. Dokumentiert ist die Schnittstelle anhand des bereitgestellten Quellstands; ein Live-Test aller Endpunkte und ein Build wurden im Rahmen der Dokumentation nicht durchgeführt.

## 8. genutzte Arduino LIBs

- Bibliothek NimBLE-Arduino in Version 1.4.3
- Bibliothek Preferences in Version 3.3.6
- Bibliothek EEPROM in Version 3.3.6
- Bibliothek WiFi in Version 3.3.6
- Bibliothek Networking in Version 3.3.6
- Bibliothek ESP Async WebServer in Version 3.6.0
- Bibliothek FS in Version 3.3.6
- Bibliothek Async TCP in Version 3.5.0
- Bibliothek ESPmDNS in Version 3.3.6
- Bibliothek ElegantOTA in Version 3.1.7
- Bibliothek Update in Version 3.3.6
- Bibliothek PubSubClient in Version 2.8
- Bibliothek WiFiManager in Version 2.0.17
- Bibliothek WebServer in Version 3.3.6
- Bibliothek DNSServer in Version 3.3.6
- Bibliothek ESP32 Async UDP in Version 3.3.6
- Bibliothek Wire in Version 3.3.6
- Bibliothek Adafruit GFX Library in Version 1.12.6
- Bibliothek Adafruit BusIO in Version 1.17.4
- Bibliothek SPI in Version 3.3.6
- Bibliothek Adafruit SSD1306 in Version 2.0.0
- Bibliothek Hash in Version 3.3.6
