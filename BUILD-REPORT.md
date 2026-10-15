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
- Appbild: 1 472 256 byte
- Apppartition: 8 388 608 byte
- Ledigt i apppartitionen: 6 916 352 byte (82 %)

ESP-IDF-bygget slutfördes utan kompilerings- eller länkfel. Bootloader, partitionstabell och appbild skrevs till kortet och esptool verifierade hashen för samtliga skrivningar.

## Startkontroll

Startloggen bekräftade:

- korrekt ESP32-S3 och 8 MB PSRAM;
- GT911-pekkontroller med ID `0x39,0x31,0x31`;
- registrerad pekinmatning och startad LVGL-task;
- startat installationsnät `Lastvakt-Setup-BC85`;
- DHCP och installationssida på `192.168.4.1`;
- startad klocka, installationssida och Tibber-fasvy utan omstart eller panik.

Riktig Tibber-data kan verifieras först när användaren har matat in Wi-Fi och en personlig Tibber-token. Fram till dess visar displayen simulerade fasvärden.

## Fabriksbackup

Före flashningen lästes hela den ursprungliga 16 MB-flashen ut:

- Fil: `lastvakt-display-factory-backup-2026-10-15.bin`
- Storlek: 16 777 216 byte
- SHA-256: `1630A7229363C3C56C089E0771F765CDF7E2E39ED5E8F1E66278A53337357BFE`

Backupen hålls utanför Git-förrådet eftersom den kan innehålla kortspecifika fabriksdata.

## Firmwarefiler

| Fil | Flashoffset | Storlek | SHA-256 |
|---|---:|---:|---|
| `bootloader.bin` | `0x0000` | 22 288 byte | `F7F368AE3AB1A69656FC47A15C656A9EAA475F79615E2457BDF796D3CC886481` |
| `partition-table.bin` | `0x8000` | 3 072 byte | `776239783A4A8CE1386FE68450F1D70759738C7269D8A8290837F45BC397D4A7` |
| `lastvakt_display.bin` | `0x10000` | 1 472 256 byte | `B7C6F408784061211B674E62BB6A65B5F63EE54E3B35BCCB1400982F93390526` |
