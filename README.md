# Smart lastvakt

En svensk klocka och fasvakt för **Waveshare ESP32-S3-Touch-LCD-4.3B-BOX** (800 × 480, ESP32-S3-N16R8). Projektet visar Tibber Pulse-strömmarna för L1, L2 och L3 och växlar automatiskt till fasvyn när någon fas går in i den röda zonen.

Standardvärdet för huvudsäkringen är **25 A**.

För projektets samlade mål, aktuella status, fattade beslut, säkerhetsprinciper, prioriterade nästa steg och överlämning till nya trådar, börja med **[PROJECT-STATUS.md](PROJECT-STATUS.md)**. Elektriska apparatuppgifter och ännu öppna frågor om COALA och elpatronen finns i [HARDWARE-NOTES.md](HARDWARE-NOTES.md).

En frikopplad, datorbaserad referensmodell för framtida tvåstegsstyrning finns beskriven i [LOAD-GUARD-POLICY.md](LOAD-GUARD-POLICY.md). Den används för simulering och tester och är inte ansluten till firmware eller fysiska reläer.

![Förhandsvisning av gränssnittet](ui-preview.svg)

## Funktioner

- Extra stor klocka, svenskt datum och automatisk CET/CEST.
- Aktuell utetemperatur i Älvsjö och ritade väderikoner för dag, natt, moln, dimma, regn, snö och åska. Vädret uppdateras var 15:e minut via [Open-Meteo](https://open-meteo.com/).
- Kortets batteribackade PCF85063A används vid start; NTP rättar tiden efter Wi-Fi-anslutning.
- Tryck på **SMART LASTVAKT** för en ren systemstatussida med Wi-Fi, tidskälla, Tibber, kalender och en QR-kod till enhetens lokala installationssida.
- Nästa händelse från familjens privata Google-kalender visas direkt på klockvyn. Tryck på kalenderfältet för en egen sida med de sex närmaste händelserna.
- Vid ett tillfälligt synkroniseringsfel visas senast hämtade kalenderdata vidare utan felvarning. Panelen jämför händelsernas sluttid med systemtiden och döljer dem automatiskt när de har passerat medan den fortsätter försöka uppdatera i bakgrunden.
- Kalenderemoji som den inbyggda skärmfonten inte kan rita ersätts med en ren punktmarkör i stället för tomma teckenrutor.
- Kalendern uppdateras var 15:e minut och vanliga dagliga, veckovisa, månatliga och årliga upprepningar stöds.
- Kalendercachen ligger i arbetsminnet och fylls därför på nytt efter omstart. Enskilda flyttade eller inställda förekomster i en återkommande serie (`EXDATE`/`RECURRENCE-ID`) stöds ännu inte fullt ut.
- Tibber GraphQL-abonnemang med `currentL1`, `currentL2` och `currentL3` i realtid.
- Lokal mätning från Shelly Plug S Gen3: **REDO**, **KÖR** eller **STRÖMLÖS** och aktuell effekt på huvudvyn samt ström, spänning och temperatur på en egen Shelly-sida.
- Ensidigt mikroskydd: när Shellyn mäter minst 500 W och färsk Tibberdata visar att total L1 är över **20,0 A** begär panelen automatiskt **AV**. Exakt 20,0 A tillåts.
- Ingen automatisk återinkoppling. Efter ett automatiskt stopp visar reläsidan aktuell L1 och kräver två sekunders långtryck samt separat bekräftelse för att slå på uttaget igen.
- Skyddad manuell relästyrning även i normal drift: tryck på mikroraden, håll styrknappen i två sekunder och bekräfta därefter ändringen i ett separat dialogfönster.
- Saknade eller äldre Tibbervärden får aldrig styra reläet; mikroskyddet kräver ett verkligt L1-värde från den aktuella anslutningen som är högst fem sekunder gammalt.
- Tibberflödet återansluts efter ren stängning, nätverks- eller protokollfel, uteblivet anslutningskvitto och om någon fas saknas eller blir för gammal. WebSocket-klienten försöker normalt igen var femte sekund; om den ändå förblir frånkopplad i 30 sekunder återskapas hela anslutningen. Misslyckad upptäckt eller start glesas stegvis ut till högst två minuter.
- Panelen hittar inte på fasvärden vid avbrott. Senaste verkliga värde visas grått i högst fem minuter och ersätts sedan med `--`; bara färska värden kan ge färgvarning eller reläbeslut.
- Ett valfritt och standardmässigt avstängt kvartspris-shadow kan nås genom att trycka på **Tibber Live**-pillret. Prissidan visar vilken högsta effekt prislagret hypotetiskt skulle tillåta nu samt de kommande 24 timmarnas `0 %`- och `50 %`-kvartar med totalpris och dygnsrang. Den är endast en simulering och har ingen koppling till Shelly, COALA, kontaktorer eller annan fysisk utgång.
- Prisplanen hålls endast i RAM och omfattar dagens och, när Tibber har publicerat den, morgondagens validerade dygn. Fram till dess visar sidan lugnt den kortare tillgängliga horisonten i stället för ett fel.
- Prisbegränsningen har en valbar, standardaktiverad sommarpaus för juni–september. Under pausen är den hypotetiska prisbegränsningen transparent, men råplanens `0 %`- och `50 %`-kvartar visas fortfarande för jämförelse. Inställningen finns på den lokala installationssidan och sparas på enheten.
- Tydliga belastningszoner: grönt till 10 A, orange till 17,5 A och rött över 17,5 A med 25 A huvudsäkring.
- Sidan **Laga mat** innehåller fyra generiska, parallella timrar: **TIMER 1** till **TIMER 4**. Varje timer öppnas genom att trycka på sitt kort.
- Tiden ställs i hela timmar och minuter med knapparna `- 1 h`, `+ 1 h`, `- 1 min` och `+ 1 min`. Knapparna kan hållas inne för upprepad ändring. Timern kan startas, pausas, fortsättas, nollställas och kvitteras när den är klar.
- Nedräkningen använder ESP32:ans monotona tid, fortsätter oberoende av vilken sida som visas och påverkas därför inte av NTP-korrigeringar. Timrarna ligger endast i RAM och nollställs om panelen startas om.
- När minst en timer är klar blinkar hela gränssnittet cyan/vitt och visar en kvitteringsknapp. Matlagningssidan behåller samtidigt en kompakt L1/L2/L3-rad med samma gröna, orange och röda belastningszoner som fasvyn.
- Automatisk fasvy i röd zon och återgång efter åtta sekunder.
- Manuell växling mellan klocka, kalender, matlagning och fasvy via pekskärmen.
- Lokal installationssida för SSID, Wi-Fi-lösenord, Tibber Personal Access Token, kalenderns hemliga iCal-adress och Shelly-adress.

## Planerade familjefunktioner

- En enkel gemensam inköpslista med möjlighet att lägga till exempelvis ”mjölk” från mobil eller display.
- Separata Shelly-pluggar till diskmaskin, tvättmaskin och torktumlare. En framtida funktion kan tolka varje maskins effektprofil som **viloläge → körning → varaktigt låg effekt → klar** och därefter skicka en notis. Push- eller SMS-tjänst är ännu inte vald. Övervakningen ska inte kunna fjärrstarta någon maskin automatiskt.
- Fortsatt bedömning av Shelly-pluggens lämplighet för mikron och apparatmanualens begränsning av extern styrning.
- Fysisk pris- och fasstyrd begränsning av elpatronens två effektsteg som ett smart tillägg i serie med den separata COALA-vakten. Kvartspolicyn, den skrivskyddade prissimuleringen och införandeplanen finns i [PRICE-CONTROL-POLICY.md](PRICE-CONTROL-POLICY.md); verifierade apparatuppgifter och öppna konstruktionsfrågor finns i [HARDWARE-NOTES.md](HARDWARE-NOTES.md).

Kalendern läses skrivskyddat via Googles hemliga iCal-adress. Adressen lagras endast på enheten, aldrig i Git-förrådet eller i loggarna.

## Första starten

1. Starta displayen och anslut en mobil eller dator till nätverket `Lastvakt-Setup-XXXX`.
2. Öppna `http://192.168.4.1`.
3. Fyll i Wi-Fi-namn, Wi-Fi-lösenord, din Tibber Personal Access Token, kalenderns **Hemliga adress i iCal-format** från Google Kalender och Shelly-pluggens lokala IP-adress. Låt sommarpausen vara markerad om prisbegränsningen ska vara inaktiv juni–september.
4. Tryck **Spara och starta om**.

Efter en lyckad anslutning finns adressen och en skanningsbar QR-kod på systemstatussidan. Tryck på **SMART LASTVAKT** på huvudvyn för att öppna den. Mobilen måste vara ansluten till samma lokala nätverk. Om Wi-Fi-anslutningen misslyckas startas installationsnätverket igen.

En personlig token kan skapas på [Tibber Developer](https://developer.tibber.com/). Firmware väljer automatiskt det första hem där realtidsmätning är aktiverad och sparar hem-id lokalt.

### Sekretess och säkerhet

SSID, lösenord, Tibber-token, kalenderadress och Shelly-adress lagras i ESP32-enhetens NVS-minne och skrivs aldrig till projektfiler eller loggar. Installationssidan använder vanlig lokal HTTP och saknar egen inloggning; använd den därför bara på ett betrott hemnätverk och låt inte installationsnätverket vara åtkomligt längre än nödvändigt. Automatiken kan bara bryta strömmen. Tillkoppling kan endast begäras manuellt från Shelly-sidan efter både ett två sekunder långt tryck och en separat bekräftelse.

20 A-funktionen är laststyrning med Tibbers och det lokala nätverkets normala fördröjning. Den ersätter inte huvudsäkring, elinstallationens skydd eller tillsyn av mikrovågsugnen.

Den kompletta användarkedjan fältprovades i hemmet 2026-10-17 med elpatronen igång, ugnen som samtidig L1-last och mikron i drift. Mikron blev automatiskt strömlös efter ungefär 30 sekunder, och en familjemedlem hittade därefter den manuella återinkopplingen på panelen utan instruktioner. Tiden är hela den observerade mikrokörningen, inte en uppmätt reaktionstid från det ögonblick då L1 passerade 20 A.

## Bygga

Projektet använder ESP-IDF 5.5.5. Hanterade komponenter hämtas automatiskt vid första byggningen.

```powershell
idf.py set-target esp32s3
idf.py build
```

## Testa styrprincipen utan hårdvara

Referensmodellerna använder endast Pythons standardbibliotek. De **65** testerna omfattar den framtida tvåstegsstyrningen för elpatronen, säker kontaktorsekvensering, 15-minutersprisernas 92/96/100-kvartdygn, juni–september-pausen och tidsbegränsade blockeringar, mikrons ensidiga 20 A-skydd samt matlagningstimrarnas tillstånd och monotona nedräkning:

```powershell
python -m unittest discover -s tests -v
python -m tools.simulate_load_guard
```

Exempelvärdena är inte installationsinställningar. Modellen har inga fysiska utgångar.

## Flasha

```powershell
idf.py -p COM11 flash monitor
```

De tre färdiga bilderna ligger även i `firmware/` och har följande offset:

| Fil | Offset |
|---|---:|
| `bootloader.bin` | `0x0000` |
| `partition-table.bin` | `0x8000` |
| `lastvakt_display.bin` | `0x10000` |

## Inställningar

`idf.py menuconfig` → **Example Configuration → Smart lastvakt** innehåller:

- huvudsäkring per fas, standard 25 A;
- gränser för orange och röd zon, standard 40 respektive 70 % av huvudsäkringen;
- hur länge fasvyn ligger kvar efter varningen, standard 8 sekunder;
- mikrons automatiska L1-frånkopplingsgräns, standard 20 A;
- Shellyeffekt som räknas som aktiv mikro, standard 500 W;
- högsta tillåtna ålder på Tibbervärdet för reläbeslut, standard 5 sekunder;
- ett standardmässigt avstängt, skrivskyddat Tibber-kvartspris-shadow med 5 `Av`- och 10 `Halv`-kvartar per validerat dygn. Det har ingen koppling till någon fysisk utgång.

Den lokala installationssidan har dessutom en standardaktiverad inställning som gör prislagret transparent under juni–september. Den råa planen förblir synlig på prissidan även under pausen.

## Struktur

- `main/network_setup.c` – Wi-Fi, NVS och den lokala installationssidan.
- `main/calendar_source.c` – hämtar, tolkar och sorterar kommande kalenderhändelser från privat iCal.
- `main/cooking_timers.c` – håller fyra parallella matlagningstimrar med monoton tid, paus, återstart och kvittering.
- `main/weather_source.c` – hämtar aktuellt väder för Älvsjö och behåller senaste värdet vid tillfälliga anslutningsfel.
- `main/shelly_client.c` – hämtar reläläge och elmätvärden och verkställer bekräftade reläkommandon i bakgrunden på det lokala nätverket.
- `main/microwave_guard.c` – övervakar färsk L1-data och kan endast begära automatisk frånkoppling av mikron; den begär aldrig tillkoppling.
- `main/tibber_client.c` – Tibbers GraphQL- och WebSocket-klient med sessionsbunden fasdata, watchdog och kontrollerad återanslutning.
- `main/tibber_price_source.c` – valfri, icke-aktuerande HTTP-källa och RAM-bok för dagens och morgondagens kompletta kvartpriser i shadow mode.
- `main/phase_source.c` – skiljer färska, partiella, senast kända och otillgängliga verkliga fasvärden åt.
- `main/app_time.c` – RTC, NTP och svensk tidszon.
- `main/ui.c` – LVGL-gränssnittet och vyväxlingen.
- `main/fonts/lv_font_lastvakt_48.c` – stor skärmfont med svenska Å, Ä och Ö för Shelly-lägena.

Kort- och bildskärmsinitieringen bygger på Waveshares officiella LVGL v8-exempel för kortet. Se [BUILD-REPORT.md](BUILD-REPORT.md) för den provade bygg- och enhetsstatusen.
