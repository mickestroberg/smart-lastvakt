# Färdig firmware

Denna uppsättning byggdes med ESP-IDF 5.5.5 och provades på Waveshare ESP32-S3-Touch-LCD-4.3B-BOX med svensk tid, aktuellt väder för Älvsjö, Tibber Pulse, lokal Shelly-mätning med skyddad relästyrning och en tryckbar översikt över de sex närmaste händelserna från privat Google-iCal. Tryck på mikroraden för Shelly-sidan; en reläändring kräver två sekunders långtryck och en separat bekräftelse. Tryck på **SMART LASTVAKT** för systemstatus och en QR-kod till den lokala installationssidan.

- `bootloader.bin` → `0x0000`
- `partition-table.bin` → `0x8000`
- `lastvakt_display.bin` → `0x10000`

Exempel:

```powershell
python -m esptool --chip esp32s3 --port COM11 --baud 460800 write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 lastvakt_display.bin
```

Projektets `BUILD-REPORT.md` innehåller storlekar och SHA-256-kontrollsummor.
