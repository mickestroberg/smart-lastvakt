# Byggrapport – Smart lastvakt

Datum: 2026-10-15

## Resultat

- Status: **byggd, flashad och startverifierad på kortet**
- Kort: Waveshare ESP32-S3-Touch-LCD-4.3B-BOX
- Port vid provning: COM11 (`VID_303A&PID_1001`)
- ESP-IDF: 5.5.5
- LVGL: 8.4.0
- Flash: 16 MB QIO, 80 MHz
- PSRAM: 8 MB OPI, 80 MHz
- Appbild: 1 394 192 byte
- Apppartition: 8 388 608 byte
- Ledigt i apppartitionen: 6 994 416 byte (83 %)

ESP-IDF-bygget slutfördes utan kompilerings- eller länkfel. Bootloader, partitionstabell och appbild skrevs till kortet och esptool verifierade hashen för samtliga skrivningar.

## Startkontroll

Startloggen bekräftade:

- korrekt ESP32-S3 och 8 MB PSRAM;
- GT911-pekkontroller med ID `0x39,0x31,0x31`;
- registrerad pekinmatning och startad LVGL-task;
- återanslutning till sparat Wi-Fi och installationssida på `192.168.10.199`;
- startad klocka, installationssida och Tibber-fasvy utan omstart eller panik.

Efter nätverkskonfiguration verifierades även:

- återanslutning till sparat Wi-Fi och svensk tid via NTP;
- godkänd Tibber-token och hämtning av ett hem med realtidsmätning;
- validerad TLS-kedja för både Tibber API och live-webbsocket;
- startad live-webbsocket utan återanslutningsfel under kontrollperioden.

TLS-arbetsminnet placeras i kortets PSRAM så att certifikatkedjan kan verifieras utan att det interna RAM-minnet tar slut. Om Tibber Pulse tillfälligt saknar data fortsätter vyn med simulerade värden och försöker återansluta automatiskt.

## Fabriksbackup

Före flashningen lästes hela den ursprungliga 16 MB-flashen ut:

- Fil: `lastvakt-display-factory-backup-2026-10-15.bin`
- Storlek: 16 777 216 byte
- SHA-256: `1630A7229363C3C56C089E0771F765CDF7E2E39ED5E8F1E66278A53337357BFE`

Backupen hålls utanför Git-förrådet eftersom den kan innehålla kortspecifika fabriksdata.

## Firmwarefiler

| Fil | Flashoffset | Storlek | SHA-256 |
|---|---:|---:|---|
| `bootloader.bin` | `0x0000` | 22 288 byte | `381F63F3142217E7CE4A87A524B81F72F197321A859B259263961399BBE0AADD` |
| `partition-table.bin` | `0x8000` | 3 072 byte | `776239783A4A8CE1386FE68450F1D70759738C7269D8A8290837F45BC397D4A7` |
| `lastvakt_display.bin` | `0x10000` | 1 394 192 byte | `C2B97BE98FF039FF690E60292F684AFDC38E4323C8EA8E14EBD2E4E926B64ABD` |
