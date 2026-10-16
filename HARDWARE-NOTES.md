# Hårdvarunoteringar

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

### Relek-elpatron – exakt modell ännu okänd

Ett fotografi visar en installerad elpatron från **Relek Produktion AB** med temperaturvred graderat från 0 och 30–90 °C samt röd manuell återställningsknapp. Ingen typskylt med exakt modell eller effekt har ännu hittats. Vredet och kapslingen räcker inte för säker identifiering eftersom samma montage- och skötselanvisning omfattar flera snarlika modeller.

Den fotograferade manualen är *Montage- & skötselanvisning för Elpatroner*, uppdaterad februari 2021, och omfattar EP1, PP1, EP2, PP2, PPS och PF. Manualen anger följande modellskillnader:

| Modellfamilj | Utförande enligt manualen | Effektlägen |
|---|---|---|
| EP1, PP1 | Ett steg, trerörselement, kopplingsbox K8, termostat 30–85 °C | Full effekt eller av |
| EP2, PP2 | Två steg, trerörselement, kopplingsbox K7 eller K8, termostat 30–90 °C | Full effekt, halv effekt eller av |
| PPS | Fyra steg, sexrörselement, kopplingsbox K7 eller K8, termostat 30–90 °C | Full, 3/4, 1/2, 1/4 eller av |
| PF 9000 W | 400 V AC3 N~, sexrörselement, kopplingsbox K8, fyrstegstermostat 30–90 °C | Har dessutom kontaktor och tidsrelä för halverad effekt |

Alla beskrivna modeller har trepoligt temperaturbegränsnings-/överhettningsskydd med manuell återställning. Manualen anger att skyddet normalt löser vid cirka 94 °C; golvvärmevarianten EP-G anges till cirka 60 °C. Elpatronen ska föregås av en allpolig strömställare/huvudbrytare.

Sidan för **PF 9000 W** är relevant men får tills vidare bara behandlas som en möjlig modell, inte som identifiering av den installerade patronen. För PF anger manualen:

- märkeffekt 9 kW vid 400 V trefas med neutralledare;
- inbyggt tidsfördröjningsrelä som halverar effekten under två timmar efter strömavbrott och vid uppstart;
- intern kontaktor för halv effekt, styrd av tidsreläet och en eventuell extern effektvakt;
- särskild anslutning `K` för signal från effektvakt; utan effektvakt byglas `F–K` enligt schemat.

Om patronen verkligen är PF 9000 W blir den beräknade resistiva fasströmmen ungefär 13,0 A vid full effekt och 6,5 A vid halv effekt vid 400 V. Ett belastningstest direkt efter spänningssättning kan då visa halv effekt i två timmar och får inte användas ensamt för att identifiera patronens märkeffekt.

PF-variantens dedikerade effektvaktsingång kan påverka den slutliga konstruktionen väsentligt. Elektrikern ska därför verifiera modell, intern koppling och vad plintarna `F` och `K` används till innan det bestäms om de externa Benedict-kontaktorerna ska bryta effekt, styra andra laster eller samverka med patronens interna kontaktor.

### Föreslagen princip

COALA behålls som fristående lokal effektvakt. En smart tvåkanalig DIN-modul läggs i serie med COALAs två styrutgångar och kan endast blockera eller tillåta kontaktorspolarna. Elpatronens trefaslast ska inte ledas genom smartmodulen, och smartmodulen får aldrig kunna kringgå pannans termostat, överhettningsskydd eller annan säkerhetskedja.

Principen är preliminär tills Relek-patronens exakta modell och befintliga styrkrets har verifierats. Om det är PF 9000 W kan dess avsedda effektvaktsingång vara lämpligare för en del av styrningen än extern brytning av huvudströmmen.

Det ger följande ansvarsfördelning:

- **COALA:** lokal och internetoberoende effektvakt.
- **Smartmodul:** fasoptimering, prisstyrning, loggning och användargränssnitt.
- **Befintlig pannstyrning:** värmebehov och apparatens ordinarie säkerhetsfunktioner.

Om smartmodulens kontakt skulle fastna sluten ska COALA fortfarande kunna koppla bort lasten. Ett manuellt, tydligt märkt **SMART/BYPASS-läge** bör övervägas så att anläggningen kan återgå till enbart COALA-drift om smartstyrningen eller nätverket fallerar.

Tibberdata som är för gammal eller saknas får aldrig ersättas med simulerade värden i relälogiken. Simulering är endast tillåten i displayvyn. Beteendet vid förlorad Tibberdata, strömavbrott och omstart ska fastställas tillsammans med elektrikern innan inkoppling.

Eftersom COALA nu är verifierad som självständig tvåstegsvakt är den föredragna programvarustrategin vid förlorad Tibberdata att smartmodulen blir transparent och tillåter båda stegen, så att COALA ensam tar över regleringen. Ett smart pris- eller Tibberbaserat stopp bör därför utformas som en tidsbegränsad blockering som måste förnyas; om styrningen försvinner ska blockeringen löpa ut. Strömavbrott i själva smartmodulen kräver fortfarande en definierad reservlösning, exempelvis SMART/BYPASS.

### Kandidat för smartmodul

**Shelly Pro 2** är en möjlig kandidat eftersom den har två potentialfria reläutgångar, lokal nätverksstyrning och en angiven bredd på cirka 19 mm. Den ryms därför nominellt i ett av de uppmätta utrymmena, men elektrikern måste kontrollera verklig passning, kapslingslock, ledningsböjar, temperatur och anslutningsutrymme.

Kontaktorspolarna är induktiva AC-laster. Lämplig stördämpning, exempelvis RC-snubber, samt eventuell användning av mellanrelä ska avgöras av elektrikern. Kontaktorernas hjälpkontakter `21–22` kan eventuellt användas som återkoppling av verkligt kontaktorläge om vald styrenhet och inkoppling lämpar sig för det.

Produktinformation: [Shelly Pro 2](https://www.shelly.com/products/shelly-pro-2).

### Kvar att fastställa

- Relek-elpatronens exakta typbeteckning, märkeffekt och typskylt.
- Om patronen har den interna tvåtimmarsfördröjningen och effektvaktsingången `F–K` som beskrivs för PF 9000 W.
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
