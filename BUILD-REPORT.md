# Byggrapport – Smart lastvakt

Datum: 2026-10-23

## Resultat

- Status: **byggd, flashad och startverifierad på kortet**
- Kort: Waveshare ESP32-S3-Touch-LCD-4.3B-BOX
- Port vid provning: COM11 (`VID_303A&PID_1001`)
- ESP-IDF: 5.5.5
- LVGL: 8.4.0
- Flash: 16 MB QIO, 80 MHz
- PSRAM: 8 MB OPI, 80 MHz
- Appbild: 1 597 184 byte (`0x185F00`)
- Apppartition: 8 388 608 byte
- Ledigt i apppartitionen: 6 791 424 byte (81 %)

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
- godkänd Tibber WebSocket-handshake, skickat GraphQL-abonnemang och mottagen komplett ny uppsättning L1/L2/L3 utan återanslutningsfel under kontrollperioden;
- NTP-synkroniserad svensk tid och automatisk minutvis NTP-återprovning om den första synkroniseringen misslyckas;
- validerad TLS-kedja till Google Kalender, fullständig iCal-hämtning och hittad nästa händelse;
- inläsning och sortering av sex kommande kalenderhändelser till den tryckbara kalenderöversikten;
- hämtning från Open-Meteo av 11,5 °C och WMO-väderkod 3 för Älvsjö vid det tidigare integrationsprovet, samtidigt som kalender och Tibber fortsatte fungera;
- skrivskyddad lokal anslutning till Shelly Plug S Gen3 på `192.168.68.121`, med avläst reläläge, effekt och cirka 239 V samtidigt som övriga datakällor fortsatte fungera;
- egen Shelly-sida med lägena REDO, KÖR och STRÖMLÖS samt två sekunders långtryck och separat bekräftelse före reläändring;
- automatisk **AV**-begäran endast när mikron drar minst 500 W och färsk Tibberdata visar L1 över 20,0 A; reläsidan visar aktuell L1 före manuell återinkoppling;
- egen 48-punktsfont med svenska tecken för Shelly-lägena samt filtrering som ersätter kalenderemoji utan skärmglyf med en punktmarkör;
- systemstatussida med aktuell nätverksadress, tydlig tidskälla, Tibber- och kalenderstatus samt QR-kod till installationssidan.

TLS-arbetsminnet placeras i kortets PSRAM och dynamiska TLS-buffertar används. Samtidigt reserveras internt DMA-minne och LVGL:s delbuffert är anpassad så att kalenderhämtning, flimmerfri bild och Tibbers liveanslutning fungerar samtidigt.

Tibber-klienten återskapar anslutningen efter ren WebSocket-stängning, nätverks- eller protokollfel, GraphQL-fel eller avslutat abonnemang. En watchdog ingriper om anslutningen inte kvitteras inom 20 sekunder, om WebSocket-klienten förblir frånkopplad i 30 sekunder eller om någon fas saknas eller inte har uppdaterats inom 30 sekunder. WebSocket-klienten gör normala återförsök var femte sekund. Misslyckad upptäckt eller start använder exponentiell fördröjning med jitter och ett tak på 120 sekunder. Varje ny WebSocket-session nollställer sina fasflaggor, så föregående sessions värden kan inte räknas som live eller användas av mikrovågsvakten. Panelen visar i stället senaste verkliga värden grått i högst fem minuter och därefter `--`. Mikrovågsvakten kräver dessutom aktuell session och ett högst fem sekunder gammalt L1-värde. Automatisk simulering används inte.

Återanslutningsutgåvan byggdes och flashades via COM11 den 2026-10-20. Flashens hash verifierades av esptool. I den levande startloggen anslöt panelen till sparat Wi-Fi och Shelly, synkroniserade svensk tid via NTP, hämtade sex kalenderhändelser och väder för Älvsjö samt genomförde Tibber-handshake och tog emot tre fasvärden. Ingen omstart eller panik observerades under kontrollperioden. Ett verkligt nätavbrott eller en framtvingad serverstängning framkallades inte; dessa återanslutningsvägar är därför kodgranskade och byggverifierade men väntar på nästa naturliga avbrott för full fältverifiering.

Samtliga **26** datorbaserade tester passerade. Sviten omfattar lastvaktsmodellen, mikrons 20 A-skydd samt matlagningstimrarnas gränser, monotona nedräkning, paus, fortsättning, avslut och kvittering.

Timerutgåvan byggdes och flashades framgångsrikt via COM11. Efter flash verifierades att panelen startade utan panik, anslöt till Wi-Fi, synkroniserade NTP, startade Tibbers websocket, läste kalendern, hämtade väder och efter automatisk återprovning anslöt till Shellyn. Den levande startloggen visade **67 343 byte internt ledigt heapminne**, **47 104 byte som största lediga DMA-block**, sex kommande kalenderhändelser, **4,1 °C** från vädertjänsten och ansluten Shelly.

Den flashade timerfunktionen innehåller fyra generiska parallella timrar. Tiden ställs i timmar och minuter, inklusive upprepad ändring vid långtryck, och varje timer stöder start, paus, fortsättning, nollställning och kvittering. Nedräkningen använder monoton tid och timerläget ligger endast i RAM, så pågående timrar återställs vid omstart. När en timer är klar blinkar displayens ram och larmfält cyan/vitt. Matlagningssidan visar samtidigt L1, L2 och L3 med samma färgzoner som den ordinarie fasvyn; energivakt och mikroskydd fortsätter arbeta oberoende.

Kalenderns avslappnade cachebeteende byggdes och flashades 2026-10-17. Startloggen verifierade en normal lyckad hämtning av sex händelser efter NTP-synkronisering. Koden behåller senast lyckade cache utan användarvarning vid tillfälliga synkfel och filtrerar lokalt bort en post när dess sparade sluttid passerar. Ett misslyckat kalenderanrop framkallades inte med avsikt på den verkliga panelen; den felvägen återstår därför att observera vid nästa naturliga, tillfälliga synkstörning.

Senare 2026-10-17 fältprovades funktionen under verklig hushållslast. Elpatronen var igång, ugnen belastade L1 och mikron användes för att smälta smör. Mikron blev automatiskt strömlös efter ungefär 30 sekunder. En familjemedlem hittade därefter reläsidan och lyckades med den manuella återinkopplingen utan instruktioner. Det verifierar det avsedda användarbeteendet från automatisk brytning till mänskligt kvitterad återinkoppling. Ingen logg eller tidsserie fångades under provet, så cirka 30 sekunder ska inte tolkas som vaktens reaktionstid från att L1 passerade 20 A.

## Offlineförberedelse för kvartspris – 2026-10-23

Ingen panel var ansluten och ingen flashning gjordes. Den frikopplade kvartspolicyn och den hårdvaruneutrala tvåstegssekvenseraren verifierades tillsammans med övriga skrivbordsmodeller: **65 av 65 tester passerade**. Testerna omfattar bland annat 92/96/100 verkliga kvartar vid svenska tidsomställningar, rangordning 5 `Av` + 10 `Halv`, standardpaus juni–september, valfri avstängning av pausen, fortsatt synlig råplan, felaktiga eller ofullständiga prisdygn, korta blockeringsleaser, prioritet för faslast samt kontaktorordning och förlorad återläsning.

Tibber-priskällan ligger bakom byggflaggan `CONFIG_LASTVAKT_TIBBER_PRICE_SHADOW`, som fortfarande är avstängd som standard. Den frågar efter både dagens och morgondagens kvartpriser, kräver ett uttryckligt sparat Tibber-hem och NTP-synkroniserad tid, validerar GraphQL-svar, valuta och varje komplett sammanhängande lokaldag och publicerar en atomisk läsbar prisbok endast i RAM/PSRAM. En giltig dag rangordnas separat. Om morgondagen ännu inte är publicerad är dagens kortare horisont ett normalt tillstånd; källan återprovar senare med jitter och exponentiell väntan och behöver inte kasta fortfarande giltig data vid ett tillfälligt fel. Ingen plan sparas till flash eller NVS.

En skrivskyddad prissida lades till bakom **Tibber Live**-pillret. Den visar vilket högsta effektläge prislagret hypotetiskt skulle tillåta just nu och paginerar de kommande 24 timmarnas `0 %`- och `50 %`-kvartar med totalpris och rang. Fram till att morgondagens prisdygn publicerats säger sidan neutralt att den visar till midnatt. Den lokala installationssidan har dessutom ett standardaktiverat val för automatisk prispaus juni–september. Under pausen är den effektiva simuleringen obegränsad men råplanen fortsätter att visas. Varken sidan, säsongspausen eller källan har någon väg till Shelly, COALA, kontaktorer eller reläutgångar.

Med shadow-flaggan på kompilerade `main/app_config.c`, `main/network_setup.c`, `main/ui.c`, `main/tibber_price_source.c` och `main/main.c` separat utan fel eller varningar. Den villkorsstyrda prisvyn i `main/ui.c` kompilerade även med flaggan av. Full arkivering och länkning kunde fortfarande inte slutföras på arbetsdatorn eftersom Windows App Control blockerade ett barnprogram till `xtensa-esp32s3-elf-ar.exe` med felkod 4551. Detta var inte ett C-kompilatorfel. Ingen firmware med prisboken eller prissidan har flashats eller liveprovats; en full ESP-IDF-byggning ska göras i den vanliga tillåtna byggmiljön innan rent shadow-läge provas på panelen.

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
| `lastvakt_display.bin` | `0x10000` | 1 597 184 byte | `24B07E3ABCFBCC927C1A71748B0D14FC9CEDE4D1185DECB6EE1A73F7FB3001D7` |
