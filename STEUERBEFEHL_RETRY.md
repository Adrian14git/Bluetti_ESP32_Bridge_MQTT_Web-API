# Automatische Wiederholung von Bluetti-Steuerbefehlen

- Gilt für alle aktiven Felder aus `bluetti_device_command[]`, unabhängig davon, ob Home Assistant MQTT oder HTTP verwendet.
- MQTT und HTTP legen Befehle in derselben Firmware-Warteschlange ab.
- Die Firmware sendet nur einen BLE-Befehl gleichzeitig und priorisiert Steuerbefehle vor Messwertabfragen.
- Erwartet wird die Modbus-Schreibbestätigung (Funktion `0x06`). Bei Timeout, ungültiger Antwort oder Modbus-Ausnahme erfolgt eine Wiederholung.
- Maximal drei Versuche insgesamt: Erstversuch + zwei Wiederholungen. Zwischen den Versuchen gilt die bestehende Bluetooth-Sperrzeit.
- Nach dem letzten Fehlschlag wird ein Diagnoseeintrag geschrieben. Ein erfolgreicher BLE-/Modbus-ACK bestätigt die Annahme des Schreibbefehls, aber nicht unabhängig den resultierenden physischen Zustand; dieser ist weiter anhand des Live-Status zu prüfen.
- Die Anzahl der Versuche ist derzeit fest im Code definiert (`MAX_CONTROL_RETRIES = 2`).

Hinweis zur Validierung: Die Änderungen wurden statisch geprüft. Ein PlatformIO-Build und ein Test mit einem realen EP500 konnten in dieser Umgebung nicht ausgeführt werden.
