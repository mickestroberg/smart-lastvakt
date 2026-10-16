# Projektstatus – Smart lastvakt

Senast uppdaterad: **2026-10-16**

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
- Shelly: **Shelly Plug S Gen3**, lokalt ansluten och provad med vattenkokare för effektvisning och relästyrning.
- Mikrovågsugn: **IKEA RAFFINERAD**, uppgiven anslutningseffekt **2300 W**.
- Shelly-pluggens uppgivna gräns enligt användaren: **2500 W**. Marginalen till mikrons märkdata är liten och permanent användning ska inte betraktas som slutligt godkänd enbart utifrån dessa två tal.

Shelly-pluggen provades först i ett uttag på L3 medan mikrons ordinarie uttag ligger på L1. Stapeln reagerade korrekt på vattenkokaren och manuell till-/frånkoppling fungerade. Shelly rapporterade vid ett tillfälle 41,9 °C trots att kapslingen inte kändes varm; värdet ska behandlas som enhetens interna temperaturmätning, inte som uppmätt yttemperatur.

## Det som fungerar på displayen

- Stor klocka, svenskt datum och automatisk CET/CEST.
- Batteribackad RTC vid start och NTP-korrigering efter nätanslutning.
- Tibber Pulse i realtid för L1, L2 och L3.
- Färgzoner: grönt till och med 10 A, orange över 10 A till och med 17,5 A och rött över 17,5 A.
- Automatisk växling till fasvyn i röd zon och återgång efter åtta sekunder.
- Nästa kalenderhändelse på huvudsidan och en tryckbar sida med sex kommande händelser.
- Privat Google-iCal, inklusive vanliga återkommande händelser.
- Utetemperatur och väderikon för Älvsjö via Open-Meteo.
- Systemstatussida via rubriken **SMART LASTVAKT**, inklusive nätstatus, tidskälla och QR-kod till installationssidan.
- Shelly-status och effekt på huvudsidan samt en egen detaljsida med ström, spänning och temperatur.
- Manuell Shelly-styrning skyddad av två sekunders långtryck och separat bekräftelse.
- Svenska tecken i Shelly-lägena och ersättning av kalenderemoji som saknar skärmglyf.
- Lokal installationssida för Wi-Fi, Tibber-token, privat kalenderadress och Shelly-adress.

Bygget är kompilerat, flashat och startverifierat på den verkliga panelen. Se [README.md](README.md) för användning och [BUILD-REPORT.md](BUILD-REPORT.md) för verifierade detaljer.

## Hårdvarufri utveckling

En frikopplad referensmodell för framtida tvåstegsstyrning finns i `tools/load_guard_policy.py`. Den har inga nätverks- eller reläutgångar och ingår inte i ESP32-firmware. Modellen gör det möjligt att testa fasgränser, stegordning, fördröjningar, återinkopplingsutrymme, datans färskhet och tidsbegränsade externa blockeringar innan den verkliga installationen är känd.

Automatiserade datorbaserade tester finns i `tests/test_load_guard_policy.py`. Se [LOAD-GUARD-POLICY.md](LOAD-GUARD-POLICY.md) för beteende, avgränsning och körning.

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
- Framtida styrning ska kontrollera mätvärdets ålder och att alla nödvändiga fasvärden är giltiga.
- Fasgränser och säkerhetsbegränsning ska ha företräde framför prisoptimering.
- En smart styrning får endast begränsa tillåten last; den får aldrig tvinga värme eller annan last att starta.

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

Den installerade patronen är från **Relek Produktion AB** och har ett vred märkt 30–90 °C samt manuell återställning. Exakt modell och märkeffekt är ännu inte identifierade.

Manualen omfattar flera modeller. **PF 9000 W** är en möjlig, men inte bekräftad, kandidat. Om det är PF-modellen har den:

- 9 kW trefaseffekt, cirka 13 A per fas vid full effekt;
- ett balanserat halvt steg på cirka 4,5 kW och 6,5 A per fas;
- två timmars halverad effekt efter strömavbrott eller uppstart;
- intern kontaktor och särskild effektvaktsingång `F–K`.

Om patronen är PF kan `F–K` vara lämpligare än att låta en ny smartmodul bryta huvudströmmen. Detta ska verifieras av elektrikern och får inte antas från kapslingens utseende.

## Mikrovågsugn och Shelly

Nuvarande firmware ger övervakning och skyddad manuell relästyrning. Automatisk frånkoppling av mikron är ännu inte färdigprojekterad.

Målbilden är att kunna hindra start när fasbelastningen är olämplig och ge familjen ett begripligt besked. Systemet får inte automatiskt återstarta en matlagningscykel eller skapa ett oövervakat återinkopplingsscenario. Shelly-pluggens verkliga marginal, temperatur och beteende med just mikron ska kontrolleras innan permanent automatisk styrning aktiveras.

## Planerade familjefunktioner

- Enkel gemensam inköpslista, exempelvis ”mjölken är slut”, med inmatning från mobil eller panel och valbar notis till de vuxna.
- Kalenderförbättringar efter familjens praktiska användning.
- Prisvisning och prisstyrning via Tibber, underordnad fas- och säkerhetsgränser.
- Säker automatisk begränsning av mikron.
- Smart, stegvis begränsning av elpatronen i samarbete med COALA.
- Spis/torrkokningsskydd har diskuterats som ett separat framtida säkerhetsproblem men ingår inte i nuvarande konstruktion.

## Nästa steg i prioriterad ordning

1. Hitta Relek-patronens typskylt eller på annat säkert sätt fastställa modell och märkeffekt.
2. Låt elektrikern fastställa vad de två Benedict-kontaktorerna matar och vilken som styrs av `Re1` respektive `Re2`.
3. Kontrollera om patronen har PF-modellens tvåtimmarsrelä och `F–K`-ingång samt om `F–K` är byglad eller ansluten.
4. Mäta strömändringen per fas för varje verkligt effektsteg.
5. Rita och godkänn slutligt kopplingsschema, inklusive reservläge, SMART/BYPASS och eventuell återkoppling från hjälpkontakterna.
6. Bestäm slutliga styrgränser, hysteres, minsta till-/fråntider och beteende vid omstart eller saknad data.
7. Verifiera Shelly-pluggens lämplighet och mikrons verkliga belastning innan automatisk mikrostyrning.
8. Ersätt referensmodellens exempelparametrar med mätta och godkända värden.
9. Porta den testade policyn till firmware och implementera fysisk relälogik först därefter.

## Öppna konstruktionsfrågor

- Exakt Relek-modell, märkeffekt och internt kopplingsschema.
- Kontaktorernas verkliga last och prioritet.
- Om en eller båda externa kontaktorerna behövs om `F–K` finns.
- Slutligt val av smart DIN-modul; Shelly Pro 2 är endast kandidat.
- Reservläge vid strömavbrott i smartmodulen.
- Placering av SMART/BYPASS.
- Behov av RC-snubber, mellanrelä och lägesåterkoppling.
- Slutlig säkerhetsbedömning av Shelly Plug S Gen3 tillsammans med mikron.
- Exakt utformning av inköpslista och familjenotiser.

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
