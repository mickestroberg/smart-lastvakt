# Byggrapport – Smart lastvakt

Datum: 2026-10-17

## Resultat

- Status: **byggd, flashad och startverifierad på kortet**
- Kort: Waveshare ESP32-S3-Touch-LCD-4.3B-BOX
- Port vid provning: COM11 (`VID_303A&PID_1001`)
- ESP-IDF: 5.5.5
- LVGL: 8.4.0
- Flash: 16 MB QIO, 80 MHz
- PSRAM: 8 MB OPI, 80 MHz
- Appbild: 1 592 560 byte
- Apppartition: 8 388 608 byte
- Ledigt i apppartitionen: 6 796 048 byte (81 %)

ESP-IDF-bygget slutfördes utan kompilerings- eller länkfel. Bootloader, partitionstabell och appbild skrevs till kortet och esptool verifierade hashen för samtliga skrivningar.

## Startkontroll

Startloggen bekräftade:

- korrekt ESP32-S3 och 8 MB PSRAM;
- GT911-pekkontroller med ID `0x39,0x31,0x31`;
- registrerad pekinmatning och startad LVGL-task;
- startad mikrovågsvakt med loggad gräns 20 A, aktivlastgräns 500 W och uttryckligt förbud mot automatisk tillkoppling;
- återanslutning till sparat Wi-Fi och installationssida på enhetens lokala IP-adress;
- startad klocka, väder, installationssida, kalender och Tibber-fasvy utan omstart eller panik.

Efter nätverkskonfiguration verifierades även:

- återanslutning till sparat Wi-Fi och svensk tid via NTP;
- godkänd Tibber-token och hämtning av ett hem med realtidsmätning;
- validerad TLS-kedja för både Tibber API och live-webbsocket;
- startad live-webbsocket utan återanslutningsfel under kontrollperioden;
- NTP-synkroniserad svensk tid och automatisk minutvis NTP-återprovning om den första synkroniseringen misslyckas;
- validerad TLS-kedja till Google Kalender, fullständig iCal-hämtning och hittad nästa händelse;
- inläsning och sortering av sex kommande kalenderhändelser till den tryckbara kalenderöversikten;
- hämtning från Open-Meteo av 11,5 °C och WMO-väderkod 3 för Älvsjö, samtidigt som kalender och Tibber fortsatte fungera;
- skrivskyddad lokal anslutning till Shelly Plug S Gen3 på `192.168.68.121`, med avläst reläläge, effekt och cirka 239 V samtidigt som övriga datakällor fortsatte fungera;
- egen Shelly-sida med lägena REDO, KÖR och STRÖMLÖS samt två sekunders långtryck och separat bekräftelse före reläändring;
- automatisk **AV**-begäran endast när mikron drar minst 500 W och färsk Tibberdata visar L1 över 20,0 A; reläsidan visar aktuell L1 före manuell återinkoppling;
- egen 48-punktsfont med svenska tecken för Shelly-lägena samt filtrering som ersätter kalenderemoji utan skärmglyf med en punktmarkör;
- systemstatussida med aktuell nätverksadress, tydlig tidskälla, Tibber- och kalenderstatus samt QR-kod till installationssidan.

TLS-arbetsminnet placeras i kortets PSRAM och dynamiska TLS-buffertar används. Samtidigt reserveras internt DMA-minne och LVGL:s delbuffert är anpassad så att kalenderhämtning, flimmerfri bild och Tibbers liveanslutning fungerar samtidigt. Om Tibber Pulse tillfälligt saknar data fortsätter vyn med simulerade värden och försöker återansluta automatiskt.

Samtliga **17** datorbaserade policytester passerade. De nya mikrovågstesterna omfattar gränsvärdet 20,0 A, aktivlastkravet, saknad/simulerad/gammal Tibberdata, avstängt eller upptaget Shellyrelä och att endast L1 används för just mikrons beslut.

Efter flash verifierades att panelen startade utan panik, anslöt till Wi-Fi, synkroniserade NTP, startade Tibbers websocket, läste kalendern, hämtade väder och efter automatisk återprovning anslöt till Shellyn med reläet fortsatt på och 0 W last.

Senare 2026-10-17 fältprovades funktionen under verklig hushållslast. Elpatronen var igång, ugnen belastade L1 och mikron användes för att smälta smör. Mikron blev automatiskt strömlös efter ungefär 30 sekunder. En familjemedlem hittade därefter reläsidan och lyckades med den manuella återinkopplingen utan instruktioner. Det verifierar det avsedda användarbeteendet från automatisk brytning till mänskligt kvitterad återinkoppling. Ingen logg eller tidsserie fångades under provet, så cirka 30 sekunder ska inte tolkas som vaktens reaktionstid från att L1 passerade 20 A.

## Fabriksbackup

Före flashningen lästes hela den ursprungliga 16 MB-flashen ut:

- Fil: `lastvakt-display-factory-backup-2026-10-15.bin`
- Storlek: 16 777 216 byte
- SHA-256: `1630A7229363C3C56C089E0771F765CDF7E2E39ED5E8F1E66278A53337357BFE`

Backupen hålls utanför Git-förrådet eftersom den kan innehålla kortspecifika fabriksdata.

## Firmwarefiler

| Fil | Flashoffset | Storlek | SHA-256 |
|---|---:|---:|---|
| `bootloader.bin` | `0x0000` | 22 288 byte | `138E55F60773EB37A68FC357992D028E5B43CC39A17AD6DD1300E82963BDBD1D` |
| `partition-table.bin` | `0x8000` | 3 072 byte | `776239783A4A8CE1386FE68450F1D70759738C7269D8A8290837F45BC397D4A7` |
| `lastvakt_display.bin` | `0x10000` | 1 592 560 byte | `E61AA76850FDBEF33DD492C7B1B05F99D87214BD67A01F2EC170E301B09BA2A2` |
