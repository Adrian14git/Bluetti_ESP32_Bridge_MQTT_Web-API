# Bluetti ESP32 Bridge – Home Assistant Paket v5

Dieses Paket bietet MQTT oder HTTP als auswählbare Messwertquelle. Die MQTT-Geräte-ID ist dynamisch und wird im Helfer `input_text.bluetti_id` gepflegt. MQTT wird über `bluetti/+/state/#` empfangen und nach Geräte-ID gefiltert; es werden keine Jinja-Templates in `state_topic` verwendet.

## Voraussetzungen
- Home Assistant mit aktivierter MQTT-Integration und verbundenem MQTT-Broker.
- Die Bridge stellt `/api/measurements` bereit.
- Die Bridge publiziert Status- und Messwerttopics in der Form `bluetti/<device_id>/state/<field>`.

## Installation
1. `packages/bluetti_bridge.yaml` nach `/config/packages/bluetti_bridge.yaml` kopieren.
2. In `/config/secrets.yaml` ergänzen:

```yaml
bluetti_bridge_api_url: "http://192.168.1.123/api/measurements"
bluetti_bridge_command_url: "http://192.168.1.123/api/command"
```

3. In `configuration.yaml` Packages aktivieren, falls noch nicht vorhanden (bestehenden `homeassistant:`-Abschnitt erweitern, nicht doppelt anlegen):

```yaml
homeassistant:
  packages: !include_dir_named packages
```

4. Home Assistant Konfiguration prüfen und neu starten.
5. Unter Einstellungen → Geräte & Dienste → Helfer die `Bluetti Geräte-ID` und `Bluetti Bridge Datenquelle` prüfen.

## Datenquellen
- MQTT: Der Router lauscht auf `bluetti/+/state/#`, akzeptiert aber nur die ID aus `input_text.bluetti_id`.
- HTTP: fragt die vollständige API-URL aus `secrets.yaml` alle 5 Sekunden ab.

## Datenqualität
Messwertsensoren sind nur verfügbar, wenn `error_code == 0`, `round_active == false` und ein Zeitstempel vorhanden ist. Die MQTT-Firmware veröffentlicht Messwerte blockweise; diese Gültigkeitsprüfung hilft, ersetzt aber keinen atomaren JSON-Snapshot. HTTP verwendet `data` nur, wenn die API einen vollständigen Datensatz liefert.

`timestamp_ms` ist die ESP32-Uptime in Millisekunden und kein Unix-Zeitstempel. Der Sensor „Bluetti Bridge Datenalter“ zeigt die Zeit seit der letzten Änderung des HA-Zeitstempelsensors, nicht die tatsächliche Geräte-Uptime-Differenz.

## Hinweise
- Die Automation und MQTT-Routing-Logik verwenden die moderne Home-Assistant-Syntax (`triggers`, `actions`, `action`). Bei älteren HA-Versionen ggf. Syntax anpassen.
- Dieses Paket wurde statisch auf YAML-Syntax geprüft, aber nicht in einer laufenden Home-Assistant-Instanz getestet.


## AC-/DC-Ausgangsschalter
Das Paket ergänzt `switch.bluetti_ac_ausgang` und `switch.bluetti_dc_ausgang`. Der Befehl wird ausschließlich über die in `input_select.bluetti_bridge_datenquelle` gewählte Quelle gesendet:
- `MQTT`: `bluetti/<Geräte-ID>/command/ac_output_on` bzw. `dc_output_on`, Payload `ON`/`OFF`.
- `HTTP`: `POST /api/command` mit JSON `{"name":"ac_output_on","value":"ON"}` bzw. `dc_output_on`.

Ergänze `bluetti_bridge_command_url` in `/config/secrets.yaml` mit der vollständigen URL zur `/api/command`-Route. Die URL muss dieselben HTTP-Authentifizierungsanforderungen erfüllen wie deine Bridge; falls HTTP Basic Authentication aktiviert ist, ist eine zusätzliche Auth-Konfiguration für `rest_command` nötig.
