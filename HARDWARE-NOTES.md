# Hårdvarunoteringar

## 2026-10-17 – Kandidat för stektermometer

Panelen har en externt tillgänglig I²C-buss på **GPIO8/SDA** och **GPIO9/SCL**. Samma buss används redan av GT911-touch, PCF85063A-RTC och CH422G-expander. Nuvarande firmware initierar bussen med **400 kHz**.

En lämplig kandidat för en framtida stektermometer är **MCP9600** tillsammans med en livsmedelsklassad typ-K-insticksprob. MCP9600 omvandlar termoelementets signal och lämnar både varm och kall temperatur via I²C. Standardadressen på Adafruits kort är `0x67`, vilket inte krockar med de adresser som nu används på panelen. MCP9600 stöder dock 100 kHz I²C, så den delade bussen måste sänkas till 100 kHz och touch, RTC samt I/O-expander därefter funktionstestas innan lösningen fastställs.

Själva MCP9600-kortet ska placeras utanför ugnen. Endast en värmetålig prob och kabel som uttryckligen är avsedd för livsmedel och ugnsbruk får föras in i ugnen; en vanlig kapslad hobbygivare eller generisk ”rostfri” termoelementprob är inte automatiskt livsmedelssäker.

Källor: [Waveshare – panelens gränssnitt och I²C-pinnar](https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B), [Microchip MCP9600](https://www.microchip.com/en-us/product/mcp9600) och [Adafruit MCP9600-guide](https://learn.adafruit.com/adafruit-mcp9600-i2c-thermocouple-amplifier/overview).

## 2026-10-17 – Mikrovågsugn och Shelly Plug S Gen3

Den installerade mikrovågsugnen är en **IKEA RAFFINERAD**. Apparatens egen typskylt anger **2100 W**. Den generella RAFFINERAD-manualen anger 2300 W märkeffekt, 1000 W mikrovågseffekt och 800 W grill; den enhetsspecifika typskylten används därför som märkdata för just denna apparat. Mikrovågseffekten 1000 W är avgiven effekt till maten och ska inte förväxlas med elektrisk ineffekt.

Shelly Plug S Gen3 flyttades 2026-10-17 till mikrons verkliga uttag på L1 och mättes direkt över det lokala RPC-gränssnittet. Reläet ändrades inte av mätningen. Under ett kort prov på full mikrovågseffekt med vatten registrerades:

- stabil aktiv effekt huvudsakligen omkring **1713–1731 W**;
- högsta observerade effekt **1741,8 W**;
- högsta observerade ström **7,913 A**;
- lägsta observerade spänning **235,3 V**;
- Shellyns interna temperatur från **43,4 °C** före provet till högst **44,2 °C**;
- efter huvudlasten cirka **26,6–26,7 W** under knappt en minut innan 0 W, sannolikt apparatens eftergång/kylning.

Ett andra, längre temperaturprov genomfördes samma dag med en liter kallt vatten och full mikrovågseffekt i **186 sekunder**. Då registrerades:

- högsta observerade effekt **1723,4 W** och ström **7,816 A**;
- lägsta spänning **235,3 V**;
- en långsam effektsänkning från ungefär 1720 W till 1640 W under körningen;
- intern Shellytemperatur **44,3 °C** före start, **46,6 °C** vid stopp och högst **46,7 °C** två minuter efter stopp;
- ungefärlig uppmätt energi **87,7 Wh**.

Temperaturökningen i det andra provet var alltså **2,4 °C**. Omedelbart efter provet kontrollerade användaren Shellyhöljet, stickpropparna och vägguttagets framsida för hand; samtliga upplevdes som **rumstempererade**. Den kontrollen visade ingen märkbar lokal uppvärmning, men är en praktisk observation och inte en kalibrerad temperaturmätning.

Avläsningen gjordes ungefär fyra gånger per sekund och gav några enstaka läsfel utan att den sammanhängande belastningskurvan förlorades. Resultaten beskriver den observerade driften men fångar inte säkert mycket korta startströmmar och utgör inte ensamma ett godkännande för permanent användning. Shelly anger 12 A och 2500 W för resistiv last; en mikrovågsugn är inte en rent resistiv last.

RAFFINERAD-manualen anger dessutom att apparaten inte får styras med extern timer eller externt fjärrkontrollsystem. Efter användarens uttryckliga beslut används Shellyn experimentellt för **automatisk frånkoppling**, men aldrig för automatisk tillkoppling eller fjärrstart. Funktionen är inte ett tillverkar- eller installationsgodkännande och den öppna manualfrågan samt Shellyns märkning för resistiv last kvarstår. Automatisk återinkoppling får inte kunna återstarta en matlagningscykel.

### Beslutad styrregel för mikron

Den första aktiva firmwareversionen använder följande regel:

- total uppmätt ström på **L1** får vara högst **20,0 A** tillsammans med mikron;
- automatisk frånkoppling begärs först när L1 är **över 20,0 A** och Shellyn samtidigt mäter minst **500 W**;
- Tibbervärdet måste vara verkligt och högst **5 sekunder** gammalt;
- simulerade, saknade eller gamla fasvärden får aldrig påverka reläet;
- automatiken får endast begära **AV** och kan aldrig begära **PÅ**;
- efter ett automatiskt stopp sker återinkoppling manuellt på panelens reläsida med två sekunders långtryck och separat bekräftelse;
- reläsidan visar aktuell L1 vid återinkoppling när färsk data finns.

Det korta mikrovågsprovet visade cirka 7,9 A som högsta observerade ström. 20 A-gränsen lämnar därmed ungefär 12 A för övriga L1-laster under den observerade mikrokörningen och cirka 5 A till huvudsäkringens nominella 25 A. Lägre vald mikrovågseffekt ska inte räknas som proportionellt lägre ögonblickslast eftersom många mikrovågsugnar reglerar med full effekt i intervaller.

Frånkopplingen arbetar med Tibbers uppdatering, lokalt nätverk, Shellyavläsning och reläkommando. Den är därför laststyrning med sekunders fördröjning, inte överströmsskydd, och ersätter inte säkring eller annan skyddsutrustning. Automatisk frånkoppling över 20 A kompilerades och flashades 2026-10-17.

Senare samma dag gav en verklig hushållssituation ett lyckat fältprov: elpatronen var igång, ugnen belastade L1 och mikron startades. Mikron blev automatiskt strömlös efter ungefär 30 sekunder. Därefter hittade en familjemedlem den skyddade manuella återinkopplingen på panelen utan instruktioner. Det bekräftar det avsedda systembeteendet i praktiken, men ingen logg fångade exakt L1-värde eller tidpunkten då 20 A passerades. Den observerade tiden får därför inte användas som mått på styrningens reaktionsfördröjning.

Källor: [IKEA RAFFINERAD-manual](https://www.ikea.com/se/sv/manuals/raffinerad-mikrovagsugn__AA-1513160-2.pdf) och [Shelly Plug S Gen3 – tekniska data](https://kb.shelly.cloud/knowledge-base/shelly-plug-s-mtr-gen3).

## 2026-10-16 – COALA och elpatronens kontaktorer

Uppgifterna nedan kommer från märkningen på den ännu inte installerade belastningsvakten och tre fotografier tagna 2026-10-16. Fotografierna är inte incheckade i Git-förrådet. All fast inkoppling och slutlig verifiering ska göras av elektrikern.

### Befintlig utrustning

- Belastningsvakt: **COALA EVS 1-3**, tillverkad i Sverige.
- Två modulära kontaktorer: **Benedict R25-31 230**.
- Märkström på kontaktorerna: `Ith 25 A`.
- Kontaktuppsättning: tre normalt öppna huvudkontakter och en normalt sluten hjälpkontakt (`21–22`) per kontaktor.
- Spolspänning: **220–240 V AC, 50 Hz** respektive 230–264 V AC, 60 Hz.
- Enligt Benedicts datablad drar spolen cirka 20–25 VA vid tillslag och 4–6 VA i draget läge.
- Fotografierna visar brun ledare från COALA mot respektive kontaktors A2 och blå ledare vid A1. Detta är en visuell observation, inte ett fastställt kopplingsschema, och ska verifieras av elektrikern.
- Uppmätt ledigt DIN-utrymme: två separata platser om ungefär **20 mm** och **25 mm**.

Datablad: [Benedict R25-serien](https://www.benedict.at/files/download/other/D994E.pdf).

### Verifierad funktion enligt COALAs pappersmanual

Följande är sammanfattat från fyra fotograferade manualsidor för EVS1-3. Bilderna är inte incheckade i Git-förrådet.

- Vakten mäter strömmen 100 gånger per sekund, lågpassfiltrerar och bestämmer den högsta fasströmmen fyra gånger per sekund. Det är den maximala fasströmmen som används av tillståndslogiken.
- `Imax` kan ställas på de fasta värdena **10, 16, 20 eller 25 A**; mellanliggande värden stöds inte.
- Hysteresen/effektreduceringsvärdet kan ställas mellan 0 och 10 kW resistiv last vid 400 V. Värdet anger hur mycket belastningen ska sjunka innan frånkopplad last får återanslutas.
- Manualen anger att hysteresvärdet ska vara större än summan av de laster som kopplas bort för att den automatiska lastavkänningen ska kunna avgöra om respektive last får plats.
- Vid uppstart dras båda interna reläerna så att båda lasterna kopplas bort under fem minuter. Fördröjningen kan hoppas över med snabbstartsknappen.
- I normalt tillstånd är båda interna reläerna fallna och ingen lastreducering är aktiv.
- Vid överström aktiveras **Re1 först** efter en progressiv fördröjning. Om överströmmen kvarstår aktiveras därefter **Re2** efter ytterligare progressiv fördröjning.
- Vid sjunkande belastning återansluts lasten som styrs av **Re2 först**. Lasten på Re1 är alltså först bort och sist tillbaka.
- Om strömmen sjunker under `Imax` minus hysteresvärdet går vakten tillbaka till normalt läge. I mellanområdet försöker den stegvis återansluta en last när den bedöms få plats.
- Om strömmen tillfälligt sjunker under referensvärdet medan nästa frånkopplingsfördröjning pågår börjar fördröjningen om.

Manualens diagram anger följande fördröjning som funktion av överlast:

| Diagrammets överlastintervall | Fördröjning |
|---|---:|
| `< 6,25 %` | 42,7 min |
| `< 12,5 %` | 21,3 min |
| `< 25 %` | 10,7 min |
| `< 37 %` | 2,7 min |
| `< 50 %` | 0,7 min |
| `> 50 %` | 0,3 min |

Vid inställningen 25 A motsvarar 50 procent överlast cirka 37,5 A på den högst belastade fasen. Tabellen ska betraktas som en återgivning av manualens diskreta diagram, inte som en fullständig säkringskurva.

Lysdioderna betyder enligt manualen:

- **Aktiv, fast grönt:** strömmen är under inställt maxvärde.
- **Aktiv, blinkande grönt:** maxvärdet överskrids, även under tiden innan någon last hunnit kopplas bort.
- **Sparläge, blinkande rött:** en last är frånkopplad.
- **Sparläge, fast rött:** båda lasterna är frånkopplade.

EVS1-3 matas med 230 V på `L` och `N`, saknar inbyggd säkring och ska enligt manualen strömförsörjas via en säkring i centralen; principskissen anger högst 10 A. Skyddsjord används inte i enheten. De tre strömtransformatorerna monteras på inkommande faser och ansluts till `S1`, `S2` och `S3`, med gemensam retur enligt manualens plintmarkering. Detta är installationsinformation för elektrikern, inte en instruktion för egen inkoppling.

### Relek-elpatron – verifierad modell

Typskylten fotograferades 2026-10-16 och identifierar den installerade patronen som **Relek PF 9 kW, 2 h spärr**. Fotografiet är inte incheckat i Git-förrådet. Följande uppgifter är avlästa direkt från typskylten:

- matning: **400 V AC3 N**;
- märkeffekt: **9 kW**;
- utförande: **2 h spärr**;
- termostat: **4-stegs EGO**;
- överhettningsskydd: **3-poligt EGO**;
- provtryck: **9 bar**;
- tillverkare: **Relek Produktion AB**.

Vid balanserad full effekt motsvarar 9 kW ungefär **13,0 A per fas**. Halv effekt, 4,5 kW, motsvarar ungefär **6,5 A per fas**. Värdena är beräknade från märkdata; verklig ström och verkliga steg ska fortfarande mätas.

Den fotograferade manualen är *Montage- & skötselanvisning för Elpatroner*, uppdaterad februari 2021, och omfattar EP1, PP1, EP2, PP2, PPS och PF. Manualen anger följande modellskillnader:

| Modellfamilj | Utförande enligt manualen | Effektlägen |
|---|---|---|
| EP1, PP1 | Ett steg, trerörselement, kopplingsbox K8, termostat 30–85 °C | Full effekt eller av |
| EP2, PP2 | Två steg, trerörselement, kopplingsbox K7 eller K8, termostat 30–90 °C | Full effekt, halv effekt eller av |
| PPS | Fyra steg, sexrörselement, kopplingsbox K7 eller K8, termostat 30–90 °C | Full, 3/4, 1/2, 1/4 eller av |
| PF 9000 W | 400 V AC3 N~, sexrörselement, kopplingsbox K8, fyrstegstermostat 30–90 °C | Har dessutom kontaktor och tidsrelä för halverad effekt |

Alla beskrivna modeller har trepoligt temperaturbegränsnings-/överhettningsskydd med manuell återställning. Manualen anger att skyddet normalt löser vid cirka 94 °C; golvvärmevarianten EP-G anges till cirka 60 °C. Elpatronen ska föregås av en allpolig strömställare/huvudbrytare.

Den identifierade patronen är alltså modellen **PF 9000 W** som beskrivs på den fotograferade manualsidan. För PF anger manualen:

- märkeffekt 9 kW vid 400 V trefas med neutralledare;
- inbyggt tidsfördröjningsrelä som halverar effekten under två timmar efter strömavbrott och vid uppstart;
- intern kontaktor för halv effekt, styrd av tidsreläet och en eventuell extern effektvakt;
- särskild anslutning `K` för signal från effektvakt; utan effektvakt byglas `F–K` enligt schemat.

Ett belastningstest direkt efter spänningssättning kan visa halv effekt i två timmar och får därför inte användas ensamt för att bedöma patronens normala fullast. Typskyltens `2 h spärr` bekräftar att denna begränsning finns på den installerade patronen.

PF-variantens dedikerade effektvaktsingång kan påverka den slutliga konstruktionen väsentligt. `F–K` och den interna kontaktorn framgår av PF-manualen men inte av typskylten; elektrikern ska därför verifiera den verkliga interna kopplingen och vad plintarna används till innan det bestäms om de externa Benedict-kontaktorerna ska bryta effekt, styra andra laster eller samverka med patronens interna kontaktor.

### Föreslagen princip

COALA behålls som fristående lokal effektvakt. En smart tvåkanalig DIN-modul läggs i serie med COALAs två styrutgångar och kan endast blockera eller tillåta kontaktorspolarna. Elpatronens trefaslast ska inte ledas genom smartmodulen, och smartmodulen får aldrig kunna kringgå pannans termostat, överhettningsskydd eller annan säkerhetskedja.

Principen är preliminär tills patronens befintliga styrkrets har verifierats. Eftersom modellen nu är bekräftad som PF 9 kW kan dess avsedda effektvaktsingång vara lämpligare för en del av styrningen än extern brytning av huvudströmmen.

Det ger följande ansvarsfördelning:

- **COALA:** lokal och internetoberoende effektvakt.
- **Smartmodul:** fasoptimering, prisstyrning, loggning och användargränssnitt.
- **Befintlig pannstyrning:** värmebehov och apparatens ordinarie säkerhetsfunktioner.

### Fastslagen princip för fasvis mätning och trefasfrånkoppling

Tibber och COALA mäter belastningen separat på L1, L2 och L3. Den högst belastade fasen får utlösa en begränsning, men elpatronen ska som grundprincip reduceras genom ett komplett och balanserat trefassteg. Endast den överbelastade fasen ska inte brytas separat. Ett sådant osymmetriskt driftläge får övervägas endast om Relek uttryckligen verifierar att den exakta patronen har oberoende avsedda enfassteg och elektrikern projekterar inkopplingen för detta.

En tidig smart gräns omkring 23 A har diskuterats för att skapa marginal till huvudsäkringens 25 A, men är ännu inte beslutad som programparameter. Slutlig gräns, hysteres och fördröjning beror på den uppmätta strömminskningen för varje verkligt effektsteg.

### Varför det finns två externa kontaktorer

COALAs två interna styrreläer är avsedda att styra varsin kontaktorspole. De två Benedict-enheterna gör den faktiska trepoliga lastbrytningen och kan därför användas för antingen två separata trefaslaster eller två balanserade trefassteg i samma last. `Re1` har lägst prioritet eftersom den lasten kopplas bort först och återansluts sist; `Re2` kopplas bort därefter och återansluts först.

Fotografierna visar inte tillräckligt för att avgöra vilka huvudlaster kontaktorerna är avsedda att mata. Det är alltså inte fastställt att en kontaktor hör till elpatronen och den andra till en annan apparat, eller att båda hör till patronens två steg. Elektrikern ska följa huvudledningarna och märka upp kontaktorerna innan detta används i styrlogiken.

Om smartmodulens kontakt skulle fastna sluten ska COALA fortfarande kunna koppla bort lasten. Ett manuellt, tydligt märkt **SMART/BYPASS-läge** bör övervägas så att anläggningen kan återgå till enbart COALA-drift om smartstyrningen eller nätverket fallerar.

Tibberdata som är för gammal eller saknas får aldrig ersättas med simulerade värden i relälogiken. Simulering är endast tillåten i displayvyn. Beteendet vid förlorad Tibberdata, strömavbrott och omstart ska fastställas tillsammans med elektrikern innan inkoppling.

Eftersom COALA nu är verifierad som självständig tvåstegsvakt är den föredragna programvarustrategin vid förlorad Tibberdata att smartmodulen blir transparent och tillåter båda stegen, så att COALA ensam tar över regleringen. Ett smart pris- eller Tibberbaserat stopp bör därför utformas som en tidsbegränsad blockering som måste förnyas; om styrningen försvinner ska blockeringen löpa ut. Strömavbrott i själva smartmodulen kräver fortfarande en definierad reservlösning, exempelvis SMART/BYPASS.

### Kandidat för smartmodul

**Shelly Pro 2** är en möjlig kandidat eftersom den har två potentialfria reläutgångar, lokal nätverksstyrning och en angiven bredd på cirka 19 mm. Den ryms därför nominellt i ett av de uppmätta utrymmena, men elektrikern måste kontrollera verklig passning, kapslingslock, ledningsböjar, temperatur och anslutningsutrymme.

Kontaktorspolarna är induktiva AC-laster. Lämplig stördämpning, exempelvis RC-snubber, samt eventuell användning av mellanrelä ska avgöras av elektrikern. Kontaktorernas hjälpkontakter `21–22` kan eventuellt användas som återkoppling av verkligt kontaktorläge om vald styrenhet och inkoppling lämpar sig för det.

Produktinformation: [Shelly Pro 2](https://www.shelly.com/products/shelly-pro-2).

### Kvar att fastställa

- Hur patronens bekräftade tvåtimmarsspärr och interna kontaktor är kopplade i den verkliga installationen.
- Om effektvaktsingången `F–K` är byglad, ansluten eller ledig.
- Vilken kontaktor som styr elpatronsteg 1 respektive steg 2.
- Effekt i kW och ström per fas för respektive steg.
- Önskat reservläge vid förlorad Tibberdata, nätverk, styrspänning eller omstart.
- Placering och utförande av SMART/BYPASS.
- Om hjälpkontakterna ska användas för lägesåterkoppling.
- Slutligt val av smartmodul och eventuell stördämpning/mellanrelä.
- Elektrikerns godkända kopplingsschema och märkning.

### Programvarukrav för en framtida styrmodul

- Separera displayens simulerade fasvärden helt från relälogiken.
- Använd färskhetskontroll på varje Tibbermätning och behandla saknade fasvärden som ogiltiga.
- Koppla bort stegvis och återinkoppla med hysteres och tidsfördröjning för att undvika kontaktorpendling.
- Ta hänsyn till COALAs egen prioritet: Re1 kopplas bort först och återkommer sist, medan Re2 kopplas bort sist och återkommer först.
- Låt fasgränser alltid ha företräde framför prisoptimering.
- Tillåt aldrig att smartstyrningen tvingar igång värme; den får endast begränsa tillåtna effektsteg.
- Låt en smart blockering löpa ut automatiskt om den inte förnyas, så att anläggningen återgår till COALA-drift vid kommunikationsfel.
- Logga mätvärde, datans ålder, beslut, kontaktororder och eventuell lägesåterkoppling.

Tibber anger att realtidsdata kan fördröjas eller grupperas och att enskilda fält kan saknas. Se [Tibbers API-referens](https://developer.tibber.com/api/reference.md).
