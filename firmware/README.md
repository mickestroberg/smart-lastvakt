# Färdig firmware

Denna uppsättning byggdes 2026-10-20 med ESP-IDF 5.5.5 och provades på Waveshare ESP32-S3-Touch-LCD-4.3B-BOX med svensk tid, aktuellt väder för Älvsjö, Tibber Pulse, lokal Shelly-mätning med skyddad relästyrning och en tryckbar översikt över de sex närmaste händelserna från privat Google-iCal. Sidan **Laga mat** har fyra oberoende, generiska timrar och visar samtidigt L1/L2/L3 med samma färgzoner som fasvyn. Varje timer kan ställas i timmar och minuter, startas, pausas, fortsättas och nollställas. När en timer är klar blinkar en cyan/vit skärmram tills larmet kvitteras. Timrarna använder monoton tid och nollställs vid omstart.

Kalendern fortsätter visa sin senast lyckade cache utan felvarning vid tillfälliga synkproblem och filtrerar lokalt bort händelser när deras sluttid passerat. När mikron drar minst 500 W bryts Shellyn automatiskt om färsk Tibberdata visar L1 över **20,0 A**. Automatiken slår aldrig på reläet. Tryck på mikroraden för Shelly-sidan; manuell återinkoppling kräver två sekunders långtryck och en separat bekräftelse. Tryck på **SMART LASTVAKT** för systemstatus och en QR-kod till den lokala installationssidan.

Tibber-klienten återansluter kontrollerat vid avbrott och bevakar både handskakningen och varje enskild fas. Efter en ny anslutning måste nya fasvärden ha kommit innan de räknas som live. Senaste verkliga värde visas grått i högst fem minuter och ersätts därefter med `--`; automatiska reläbeslut kräver aktuell session och högst fem sekunder gammal verklig L1-data. Firmware använder ingen automatisk simulering.

- `bootloader.bin` → `0x0000`
- `partition-table.bin` → `0x8000`
- `lastvakt_display.bin` → `0x10000`

Exempel:

```powershell
python -m esptool --chip esp32s3 --port COM11 --baud 460800 write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 lastvakt_display.bin
```

Projektets `BUILD-REPORT.md` innehåller storlekar och SHA-256-kontrollsummor.
