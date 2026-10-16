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

### Föreslagen princip

COALA behålls som fristående lokal effektvakt. En smart tvåkanalig DIN-modul läggs i serie med COALAs två styrutgångar och kan endast blockera eller tillåta kontaktorspolarna. Elpatronens trefaslast ska inte ledas genom smartmodulen, och smartmodulen får aldrig kunna kringgå pannans termostat, överhettningsskydd eller annan säkerhetskedja.

Det ger följande ansvarsfördelning:

- **COALA:** lokal och internetoberoende effektvakt.
- **Smartmodul:** fasoptimering, prisstyrning, loggning och användargränssnitt.
- **Befintlig pannstyrning:** värmebehov och apparatens ordinarie säkerhetsfunktioner.

Om smartmodulens kontakt skulle fastna sluten ska COALA fortfarande kunna koppla bort lasten. Ett manuellt, tydligt märkt **SMART/BYPASS-läge** bör övervägas så att anläggningen kan återgå till enbart COALA-drift om smartstyrningen eller nätverket fallerar.

Tibberdata som är för gammal eller saknas får aldrig ersättas med simulerade värden i relälogiken. Simulering är endast tillåten i displayvyn. Beteendet vid förlorad Tibberdata, strömavbrott och omstart ska fastställas tillsammans med elektrikern innan inkoppling.

### Kandidat för smartmodul

**Shelly Pro 2** är en möjlig kandidat eftersom den har två potentialfria reläutgångar, lokal nätverksstyrning och en angiven bredd på cirka 19 mm. Den ryms därför nominellt i ett av de uppmätta utrymmena, men elektrikern måste kontrollera verklig passning, kapslingslock, ledningsböjar, temperatur och anslutningsutrymme.

Kontaktorspolarna är induktiva AC-laster. Lämplig stördämpning, exempelvis RC-snubber, samt eventuell användning av mellanrelä ska avgöras av elektrikern. Kontaktorernas hjälpkontakter `21–22` kan eventuellt användas som återkoppling av verkligt kontaktorläge om vald styrenhet och inkoppling lämpar sig för det.

Produktinformation: [Shelly Pro 2](https://www.shelly.com/products/shelly-pro-2).

### Kvar att fastställa

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
- Låt fasgränser alltid ha företräde framför prisoptimering.
- Tillåt aldrig att smartstyrningen tvingar igång värme; den får endast begränsa tillåtna effektsteg.
- Logga mätvärde, datans ålder, beslut, kontaktororder och eventuell lägesåterkoppling.

Tibber anger att realtidsdata kan fördröjas eller grupperas och att enskilda fält kan saknas. Se [Tibbers API-referens](https://developer.tibber.com/api/reference.md).
