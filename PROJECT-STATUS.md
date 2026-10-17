# Projektstatus – Smart lastvakt

Senast uppdaterad: **2026-10-17**

Detta är projektets gemensamma överlämningsdokument. En ny tråd ska kunna börja här utan att läsa hela chatthistoriken. Tekniska detaljer finns i dokumenten som länkas nedan.

## Syfte och mål

Smart lastvakt är en familjevänlig informations- och styrpanel för hemmet. Den ska göra tre saker:

1. Visa vardagsinformation tydligt: stor svensk klocka, kalender och väder.
2. Visa den verkliga belastningen på L1, L2 och L3 så att familjen förstår när stora laster kan startas.
3. På sikt begränsa utvalda laster säkert och förutsägbart utan att kringgå lokal skyddsutrustning.

Projektet började som en lastvakt för mikrovågsugnen men har vuxit till en allmän familjepanel och ett möjligt smart komplement till elpatronens separata COALA-vakt. Bekvämlighetsfunktioner får aldrig ges företräde framför elsäkerhet, termostat, överhettningsskydd eller lokal effektvakt.

## Källkod och viktiga platser

- GitHub: <https://github.com/mickestroberg/smart-lastvakt>
- Huvudgren: `main`
- Lokal arbetskopia: `C:\Users\hp\Documents\Codex\2026-10-15\referenced-chatgpt-conversation-this-is-an\outputs\lastvakt-display`
- Fabriksbackup, utanför Git: `C:\Users\hp\Documents\Codex\2026-10-15\referenced-chatgpt-conversation-this-is-an\outputs\lastvakt-display-factory-backup-2026-10-15.bin`
- Fabriksbackupens SHA-256: `1630A7229363C3C56C089E0771F765CDF7E2E39ED5E8F1E66278A53337357BFE`

Fabriksbackupen kan innehålla enhetsspecifika data och ska inte läggas i Git. Den fullständiga bygg- och flashrapporten finns i [BUILD-REPORT.md](BUILD-REPORT.md).

## Bekräftad maskinvara och miljö

- Display: **Waveshare ESP32-S3-Touch-LCD-4.3B-BOX**, 800 × 480.
- Styrenhet: ESP32-S3-N16R8, 16 MB flash och 8 MB PSRAM.
- Senast verifierade serieport: **COM11**, USB-ID `VID_303A&PID_1001`.
- Utvecklingsmiljö: Windows och ESP-IDF **5.5.5**.
- Huvudsäkringar: **25 A per fas**.
- Tibber Pulse: L1/L2/L3 har jämförts med Tibber-appen och värdena stämde exakt vid kontrollen.
- Shelly: **Shelly Plug S Gen3**, lokalt ansluten och provad med både vattenkokare och den verkliga mikrovågsugnen.
- Mikrovågsugn: **IKEA RAFFINERAD**, vars egen typskylt anger **2100 W**. Den generella manualens 2300 W behandlas som annan variant eller generell maxuppgift.
- Shelly anger **12 A** och **2500 W för resistiv last**. Mikron är inte en rent resistiv last, så märkdata och ett kort prov räcker inte ensamma som slutligt godkännande för permanent brytning.

Shelly-pluggen provades först i ett uttag på L3 medan mikrons ordinarie uttag ligger på L1. Stapeln reagerade korrekt på vattenkokaren och manuell till-/frånkoppling fungerade. Shelly rapporterade vid ett tillfälle 41,9 °C trots att kapslingen inte kändes varm; värdet ska behandlas som enhetens interna temperaturmätning, inte som uppmätt yttemperatur.

Den 2026-10-17 flyttades pluggen till mikrons verkliga uttag på L1. Ett kort prov på full mikrovågseffekt gav högst **1741,8 W** och **7,913 A**, med lägst **235,3 V**. Shellyns interna temperatur steg från 43,4 till högst 44,2 °C. Efter huvudlasten låg mikron omkring 26,7 W under knappt en minut innan den gick till 0 W. Detaljer och avgränsningar finns i [HARDWARE-NOTES.md](HARDWARE-NOTES.md).

Ett efterföljande treminutersprov med en liter vatten gav högst **1723,4 W**, **7,816 A** och 235,3 V. Den interna temperaturen ökade från 44,3 till högst 46,7 °C, varav toppvärdet uppmättes två minuter efter stopp. Provets energi var cirka 87,7 Wh. Direkt efteråt upplevdes Shellyhölje, stickproppar och vägguttag som rumstempererade vid manuell kontroll.

## Det som fungerar på displayen

- Stor klocka, svenskt datum och automatisk CET/CEST.
- Batteribackad RTC vid start och NTP-korrigering efter nätanslutning.
- Tibber Pulse i realtid för L1, L2 och L3.
- Färgzoner: grönt till och med 10 A, orange över 10 A till och med 17,5 A och rött över 17,5 A.
- Fyra generiska och parallella matlagningstimrar med inställning i timmar och minuter, upprepad ändring vid långtryck samt start, paus, fortsättning, nollställning och kvittering.
- Timerlogiken använder monoton tid och fortsätter oberoende av vald sida. Tillstånden ligger endast i RAM och återställs vid omstart.
- En färdig timer ger ett globalt cyan/vitt blinkande larm tills den kvitteras. Matlagningssidan visar samtidigt L1, L2 och L3 med samma färgzoner som den fullständiga fasvyn.
- Automatisk växling till fasvyn i röd zon och återgång efter åtta sekunder.
- Nästa kalenderhändelse på huvudsidan och en tryckbar sida med sex kommande händelser.
- Privat Google-iCal, inklusive vanliga återkommande händelser.
- Avslappnad kalendercache: tillfälliga synkfel visas inte för familjen när användbar cache finns, och passerade poster filtreras lokalt mot systemtiden.
- Utetemperatur och väderikon för Älvsjö via Open-Meteo.
- Systemstatussida via rubriken **SMART LASTVAKT**, inklusive nätstatus, tidskälla och QR-kod till installationssidan.
- Shelly-status och effekt på huvudsidan samt en egen detaljsida med ström, spänning och temperatur.
- Manuell Shelly-styrning skyddad av två sekunders långtryck och separat bekräftelse.
- Automatisk frånkoppling av mikron när den är aktiv och färsk Tibberdata visar L1 över 20,0 A. Automatiken kan aldrig slå på uttaget igen.
- Reläsidan visar autoskyddets tillgänglighet, utlösande L1 och aktuell L1 inför manuell återinkoppling.
- Svenska tecken i Shelly-lägena och ersättning av kalenderemoji som saknar skärmglyf.
- Lokal installationssida för Wi-Fi, Tibber-token, privat kalenderadress och Shelly-adress.

20 A-versionen kompilerades, flashades och startverifierades på den verkliga panelen 2026-10-17. Senare samma dag gav verklig samtidig hushållslast ett lyckat fältprov av både automatisk frånkoppling och manuell återinkoppling. Timerutgåvan byggdes därefter, flashades via COM11 och startverifierades med övriga livekällor anslutna. Samtliga 26 datorbaserade tester passerade. Se [README.md](README.md) för användning och [BUILD-REPORT.md](BUILD-REPORT.md) för verifierade detaljer.

## Hårdvarufri utveckling

En frikopplad referensmodell för framtida tvåstegsstyrning finns i `tools/load_guard_policy.py`. Den har inga nätverks- eller reläutgångar och ingår inte i ESP32-firmware. Modellen gör det möjligt att testa fasgränser, stegordning, fördröjningar, återinkopplingsutrymme, datans färskhet och tidsbegränsade externa blockeringar innan den verkliga installationen är känd.

Automatiserade datorbaserade tester finns i `tests/test_load_guard_policy.py`. Mikrons aktiva envägsvakt har dessutom en ren beslutsmodell i `tools/microwave_guard_policy.py` med tester i `tests/test_microwave_guard_policy.py`. Matlagningstimrarnas rena tillståndsmodell finns i `tools/cooking_timer_policy.py` och provas av `tests/test_cooking_timer_policy.py`. Den samlade sviten innehåller 26 tester. Se [LOAD-GUARD-POLICY.md](LOAD-GUARD-POLICY.md) för lastvaktens beteende, avgränsning och körning.

## Sekretess och konfiguration

Följande får aldrig checkas in, skrivas i dokumentation eller återges i loggutdrag:

- Wi-Fi-SSID och lösenord;
- Tibber Personal Access Token;
- Googles hemliga iCal-adress;
- andra autentiseringsuppgifter eller exporter av enhetens NVS.

Uppgifterna lagras i ESP32-enhetens NVS och matas in via den lokala installationssidan. Tibber-token hämtades tidigare från användarens lokala fil och lades direkt i enheten; tokenvärdet hör inte hemma i projektet.

## Beslutade systemprinciper

### Display och mätdata

- Displayen får visa simulerade fasvärden när Tibber saknas, men simulerade värden får **aldrig** användas för reläbeslut.
- All relästyrning ska kontrollera mätvärdets ålder och att alla nödvändiga fasvärden är giltiga.
- Fasgränser och säkerhetsbegränsning ska ha företräde framför prisoptimering.
- En smart styrning får endast begränsa tillåten last; den får aldrig tvinga värme eller annan last att starta.

### Mikrovågsugn

- Total L1 får vara högst **20,0 A** tillsammans med mikron; automatisk frånkoppling sker först över gränsen.
- Mikron bedöms vara aktiv när Shellyn mäter minst **500 W**.
- Tibbervärdet måste vara verkligt och högst **5 sekunder** gammalt för att få styra reläet.
- Automatiken kan bara begära **AV**. Återinkoppling är alltid manuell med två sekunders långtryck och separat bekräftelse.
- Detta är laststyrning med nätverks- och mätfördröjning, inte ett överströmsskydd och inte en ersättning för 25 A-huvudsäkringen.

### Elpatron och trefas

- Belastningen mäts per fas och den högst belastade fasen kan utlösa en åtgärd.
- Själva elpatronen ska som grundprincip reduceras i **balanserade trefassteg**. Endast den överbelastade fasen ska inte brytas separat, såvida inte Relek uttryckligen verifierar att patronen har oberoende avsedda enfassteg.
- Ett diskuterat tidigt ingripande är cirka **23 A**, men detta är ännu inte en fast programinställning. Slutliga nivåer, hysteres och fördröjningar ska bestämmas efter att patronens verkliga effektsteg har mätts.
- COALA ska behållas som lokal, internetoberoende effektvakt.
- Vid gammal eller saknad Tibberdata ska smartstyrningen bli transparent så att COALA ensam kan arbeta.
- Smart blockering ska vara tidsbegränsad och behöva förnyas; kommunikationsfel ska inte kunna lämna värmen permanent blockerad.
- Ett tydligt **SMART/BYPASS-läge** ska övervägas.
- All fast installation och slutligt kopplingsschema tillhör elektrikern.

COALA, kontaktorerna och Relek-patronen dokumenteras utförligt i [HARDWARE-NOTES.md](HARDWARE-NOTES.md).

## COALA och kontaktorerna i korthet

- Belastningsvakten är **COALA EVS 1-3** med två reduktionssteg.
- Den mäter alla tre faserna och använder den högsta fasströmmen.
- `Imax` kan ställas till 10, 16, 20 eller 25 A; den aktuella anläggningen har 25 A huvudsäkringar.
- `Re1` kopplas bort först och återkommer sist. `Re2` kopplas bort därefter och återkommer först.
- De två grå enheterna är **Benedict R25-31 230-kontaktorer**, inte mätande reläer. COALAs mindre reläutgångar styr deras spolar.
- Två kontaktorer gör det möjligt att bryta två separata trefaslaster eller två balanserade trefassteg i samma last.
- Fotografierna räcker inte för att fastställa vad kontaktor 1 och 2 faktiskt ska mata. De ska inte tillskrivas varsin last innan elektrikern har följt ledningarna.

## Relek-elpatronen

Typskylten fotograferades 2026-10-16 och bekräftar att den installerade patronen är **Relek PF 9 kW, 2 h spärr**, matad med **400 V AC3 N**. Typskylten anger också fyrstegstermostat, trepoligt överhettningsskydd och 9 bars provtryck.

Den bekräftade PF-modellen har enligt märkning och manual:

- 9 kW trefaseffekt, cirka 13 A per fas vid full effekt;
- ett balanserat halvt steg på cirka 4,5 kW och 6,5 A per fas;
- två timmars halverad effekt efter strömavbrott eller uppstart;
- intern kontaktor och särskild effektvaktsingång `F–K`.

`F–K` kan vara lämpligare än att låta en ny smartmodul bryta huvudströmmen. Ingången och den verkliga interna kopplingen ska ändå verifieras av elektrikern; `F–K` framgår av manualen men inte av typskylten.

## Mikrovågsugn och Shelly

Nuvarande firmware ger övervakning, skyddad manuell relästyrning och automatisk **frånkoppling** enligt 20 A-regeln ovan. Efter auto-stopp ligger uttaget av tills en människa återinkopplar det på reläsidan. Systemet innehåller ingen automatisk tillkoppling och kan därför inte starta en matlagningscykel efter att lasten har sjunkit.

Funktionen är avsiktligt försiktig med datakvalitet: om Tibbervärdet är simulerat, saknas eller är äldre än fem sekunder gör automatiken ingenting och visar att skyddet inte är tillgängligt. Den 2026-10-17 provades hela användarkedjan i verklig drift: elpatronen var igång, ugnen belastade L1 och mikron startades. Mikron blev automatiskt strömlös efter ungefär 30 sekunder, varefter användaren hittade den manuella återinkopplingen på panelen utan instruktioner. Ingen tidsserie sparades, så de 30 sekunderna är inte vaktens uppmätta reaktionstid; tidpunkten då L1 faktiskt passerade 20 A är okänd. IKEA-manualens formulering om extern styrning och Shellyns märkning för resistiv last är fortfarande öppna frågor för permanent användning.

## Matlagningstimrar

Sidan **Laga mat** har fyra likadana parallella timrar, benämnda **TIMER 1** till **TIMER 4**. Ett tryck på ett timerkort öppnar inställningen. Timmar och minuter kan minskas eller ökas med korta tryck eller långtryck för upprepad ändring. Varje timer kan startas, pausas och fortsättas, nollställas eller kvitteras när den är klar.

Nedräkningen beräknas mot ESP32:ans monotona tid och påverkas därför inte av NTP- eller RTC-korrigeringar. Timrarna fortsätter när användaren lämnar matlagningssidan, men lagras inte beständigt; samtliga återställs om panelen startas om. När en eller flera timrar är klara blinkar gränssnittet cyan/vitt och visar hur många som väntar på kvittering. Matlagningssidan visar hela tiden en kompakt L1/L2/L3-rad med projektets vanliga gröna, orange och röda zoner. Energiövervakningen och mikrons separata skydd fortsätter oberoende av timersidan.

## Planerade familjefunktioner

- Möjlig stektermometer: livsmedelsklassad typ-K-insticksprob via en I²C-omvandlare av typen MCP9600. Elektroniken ska sitta utanför ugnen; endast en värmetålig, uttryckligen livsmedelsavsedd prob och kabel får gå in i ugnsutrymmet.
- Separata Shelly-pluggar till diskmaskin, tvättmaskin och torktumlare. Avslutad körning ska i första hand identifieras lokalt från respektive effektprofil: **viloläge → körning → varaktigt låg effekt → klar**. Trösklar och varaktighet måste mätas in per maskin för att undvika falska färdigmeddelanden.
- En senare notistjänst kan skicka pushmeddelande eller SMS när en maskin bedöms vara klar. Tjänst och leveranssätt är ännu inte valda. Funktionen ska vara övervakande och får inte ge automatisk fjärrstart.
- Enkel gemensam inköpslista, exempelvis ”mjölken är slut”, med inmatning från mobil eller panel och valbar notis till de vuxna.
- Kalenderförbättringar efter familjens praktiska användning.
- Prisvisning och prisstyrning via Tibber, underordnad fas- och säkerhetsgränser.
- Möjlig förvarning eller startspärr för mikron innan 20 A-vakten behöver bryta en pågående körning.
- Smart, stegvis begränsning av elpatronen i samarbete med COALA.
- Spis/torrkokningsskydd har diskuterats som ett separat framtida säkerhetsproblem men ingår inte i nuvarande konstruktion.

## Nästa steg i prioriterad ordning

1. Lägg till en liten lokal händelselogg med tidsstämplar för mikrovakten, L1-värde, Tibbervärdets ålder, Shellyeffekt och reläkommando så att nästa verkliga stopp kan analyseras i efterhand.
2. Gör en slutlig säkerhetsbedömning av Shelly-pluggens lämplighet för mikron, inklusive korta startströmmar och IKEA-manualens begränsning av extern styrning.
3. Låt elektrikern fastställa vad de två Benedict-kontaktorerna matar och vilken som styrs av `Re1` respektive `Re2`.
4. Kontrollera den bekräftade PF-patronens interna kontaktor och om effektvaktsingången `F–K` är byglad, ansluten eller ledig.
5. Verifiera tvåtimmarsspärrens praktiska beteende utan att förväxla ett halvlastprov direkt efter spänningssättning med patronens fulla effekt.
6. Mäta strömändringen per fas för varje verkligt effektsteg.
7. Rita och godkänn slutligt kopplingsschema, inklusive reservläge, SMART/BYPASS och eventuell återkoppling från hjälpkontakterna.
8. Bestäm slutliga styrgränser, hysteres, minsta till-/fråntider och beteende vid omstart eller saknad data.
9. Ersätt referensmodellens exempelparametrar med mätta och godkända värden.
10. Porta den testade policyn till firmware och implementera fysisk relälogik först därefter.

## Öppna konstruktionsfrågor

- Relek-patronens verkliga interna koppling kring kontaktor, tvåtimmarsspärr och `F–K`.
- Kontaktorernas verkliga last och prioritet.
- Om en eller båda externa kontaktorerna behövs om `F–K` finns.
- Slutligt val av smart DIN-modul; Shelly Pro 2 är endast kandidat.
- Reservläge vid strömavbrott i smartmodulen.
- Placering av SMART/BYPASS.
- Behov av RC-snubber, mellanrelä och lägesåterkoppling.
- Slutlig säkerhetsbedömning av Shelly Plug S Gen3 tillsammans med mikron.
- Hur matlagningstimrarna ska larma hörbart. Kortets officiella pinout och schema visar ingen summer eller högtalare som ännu kan betraktas som verifierad i vår BOX-enhet.
- Om MCP9600 kan dela panelens befintliga I²C-buss stabilt vid 100 kHz med touch, RTC och I/O-expander, eller om temperaturdelen ska få en separat anslutning.
- Val av lokal klardetektering, trösklar och push- eller SMS-tjänst för framtida Shelly-övervakning av diskmaskin, tvättmaskin och torktumlare.
- Exakt utformning av inköpslista och familjenotiser.
- Fullständigt stöd för enskilda flyttade eller inställda kalenderförekomster i återkommande serier (`EXDATE`/`RECURRENCE-ID`).

## Dokumentkarta

- [README.md](README.md) – funktioner, användning, installation, konfiguration, byggning och flashning.
- [PROJECT-STATUS.md](PROJECT-STATUS.md) – mål, nuläge, beslut, prioriteringar och överlämning.
- [HARDWARE-NOTES.md](HARDWARE-NOTES.md) – verifierade apparatuppgifter, COALA-logik, elpatron och säkerhetskrav.
- [LOAD-GUARD-POLICY.md](LOAD-GUARD-POLICY.md) – icke-aktuerande referensmodell, tillståndsregler och automatiserade tester.
- [BUILD-REPORT.md](BUILD-REPORT.md) – verifierat bygge, verklig enhet, firmwarestorlekar och fabriksbackup.
- [firmware/README.md](firmware/README.md) – färdiga binärfiler och flashoffset.
- [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) – externa komponenter och datakällor.

## Överlämning till en ny tråd

En ny Work-tråd ska:

1. arbeta i GitHub-repot eller den lokala arbetskopian ovan;
2. läsa detta dokument och därefter relevant detaljdokument;
3. kontrollera Git-status innan ändringar;
4. skilja bekräftade fakta från antaganden;
5. inte exponera eller checka in hemligheter;
6. inte ändra fast elinstallation eller anta ett kopplingsschema från fotografier;
7. uppdatera detta dokument när mål, beslut, verifierad status eller nästa steg ändras.
