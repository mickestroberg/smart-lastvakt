# Kvartsprisstyrning för elpatronen

Detta dokument beskriver den planerade prisbegränsningen av Relek PF 9 kW och dess samverkan med fasvakten och COALA. Den körbara referensen finns i `tools/quarter_price_policy.py` och testas av `tests/test_quarter_price_policy.py`.

Beslutsreferensen är **inte** inkopplad till någon fysisk utgång. Firmware innehåller nu dessutom en separat, standardmässigt avstängd Tibber-källa som kan hämta och rangordna dagens priser i skrivskyddat shadow mode. Den har ingen beroendekedja till Shelly, COALA eller relä-API. Prisfunktionen ska förbli bortkopplad från verklig styrning tills elektrikern har identifierat de två effektstegen, godkänt kopplingen och ett säkert reservläge har verifierats.

## Grundregel

Prislagret anger bara den högsta effekt som smartstyrningen får lämna tillgänglig. Det får aldrig begära att värme startas.

| Prisnivå | Minsta antal blockerade steg | Betydelse |
|---|---:|---|
| Full | 0 | Ingen prisbegränsning; övriga skydd bestämmer |
| Halv | 1 | Minst Re1 blockeras |
| Av | 2 | Båda smartstyrda stegen blockeras |

Standardförslaget för ett dygn är:

- de **5 dyraste** kvartarna: `Av`;
- plats **6–15**, alltså följande **10** kvartar: `Halv`;
- övriga kvartar: `Full`.

Detta är en första optimeringsstrategi, inte en fast komfortinställning. Innan `Av` används fysiskt ska vi besluta om maximal sammanhängande avstängning och helst ha pann-/tanktemperatur eller annan verifierad komfortgräns. En försiktig första driftsättning kan begränsa prislagret till högst `Halv` medan endast tänkta `Av`-beslut loggas.

## Säkerhets- och prioritetsordning

1. Säkringar, pannans överhettningsskydd, termostat och övrig ordinarie pannstyrning.
2. COALA som självständig, lokal och internetoberoende lastvakt.
3. Fysisk SMART/BYPASS, där BYPASS betyder enbart COALA-drift.
4. Manuell smart blockering.
5. Faslastpolicyn, styrd av den högst belastade fasen och endast av färsk verklig data.
6. Prisbegränsningen.
7. Grundläget utan smart begränsning.

Blockerande källor kombineras konservativt: det högsta begärda antalet blockerade steg vinner. Billigt pris kan därför aldrig häva en blockering som fasvakten eller ett manuellt stopp kräver. Med färsk fasdata får ett steg efter en prisperiod återvända först efter verifierat strömutrymme, återinkopplingsfördröjning och minsta ändringsintervall. Om fasdata i stället saknas eller är gammal blir smartlogikens mål transparent; med fungerande och färsk återläsning går smartutgångarna stegvis mot tillåtet läge och COALA ensam avgör om kontaktorerna verkligen får dra. Saknad utgångsåterläsning ger däremot inga automatiska `PERMIT`-kommandon och kräver felsökning eller fysisk BYPASS.

Elpatronen ska fortsätta reduceras i kompletta, balanserade trefassteg. En ensam fas ska inte brytas.

## Dygn, rangordning och sommartid

Priserna grupperas efter lokal kalenderdag i `Europe/Stockholm`, men varje kvart identifieras med sin unika UTC-starttid. Ett normalt dygn har 96 kvartar. Dygnet då sommartid börjar har 92, och dygnet då normaltid återkommer har 100. Kvoterna 5 `Av` och 10 `Halv` behålls; antalet `Full` blir alltså 81, 77 respektive 85.

Rangordningen använder `total` från Tibber, dyrast först. Negativa priser är giltiga. Vid exakt lika pris avgör tidigare UTC-start, så resultatet är deterministiskt och oberoende av ordningen i API-svaret.

En plan godtas bara när hela lokaldagen är komplett, unik och sammanhängande i 15-minutersintervall och alla priser är ändliga. Luckor, dubbletter, fel tid, trasiga värden eller opålitlig systemtid ger ingen ny prisblockering.

## Tibber som prisdatakälla

Den befintliga Tibber-token kan användas även för priser. Tibbers GraphQL-schema stöder uttryckligen kvartupplösning:

```graphql
{
  viewer {
    homes {
      currentSubscription {
        priceInfo(resolution: QUARTER_HOURLY) {
          today    { total energy tax startsAt currency level }
          tomorrow { total energy tax startsAt currency level }
        }
      }
    }
  }
}
```

`total` används för hushållets rangordning. Det omfattar Tibbers redovisade energipris och skatt men ska inte antas innehålla alla nätägarens effekt- eller överföringsavgifter. Valutan kontrolleras och sparas uttryckligen. Nord Pool behöver därför inget separat konto eller token för den första versionen.

Prisfrågan ska ligga i en separat HTTP-modul och inte byggas in i den nu stabiliserade Pulse-WebSocket-klienten. `today` hämtas efter tids- och nätstart. `tomorrow` efterfrågas med randomiserad fördröjning under eftermiddagen och återprovas med jitter och exponentiell fördröjning tills en komplett plan finns; ett tomt `tomorrow` räknas alltså inte som färdigt dygnsarbete och ska inte visas som ett familjefel. En senare hämtning får atomiskt ersätta en tidigare preliminär plan först när hela den nya dagen har validerats. Exakt publiceringsklockslag ska inte hårdkodas.

Källor: [Tibbers GraphQL-schema](https://developer.tibber.com/api/reference.md), [Tibbers API-guide](https://developer.tibber.com/docs/guides/calling-api), [Nord Pools övergång till 15 minuters marknadstid](https://www.nordpoolgroup.com/en/trading/transition-to-15-minute-market-time-unit-mtu/).

## Cache, klocka och tidsbegränsade beslut

- En validerad prisplan kan sparas lokalt för i dag och i morgon.
- Den aktiva kvarten är ett halvöppet intervall `[start, slut)`, så exakt kvartsskifte väljer den nya kvarten utan glapp.
- En begränsning översätts till en kort monoton lease, normalt högst 60 sekunder och aldrig längre än den aktuella kvartens ursprungliga sluttid. Ett fungerande kontrollflöde förnyar den medan samma begränsning gäller.
- Ett nät- eller API-fel får inte förlänga leasen.
- Vid omstart krävs både validerad cache och trovärdig väggklocka; annars är prislagret transparent.
- Prisleasen lagras inte som permanent relätillstånd.

Referens-API:t har en uttrycklig `time_trusted`-grind. `app_time` behöver före firmwareintegrationen exponera tidskvalitet, exempelvis okänd byggtid, RTC och NTP, och mata denna grind. NTP är bäst. Om RTC ensam ska få aktivera en sparad prisplan är ett separat beslut; en tidszonsmedveten men felställd klocka får inte betraktas som trovärdig bara på grund av sitt format.

## Reläsekvensering och återkoppling

Den rena pris- och lastreferensen kan logiskt begära 0, 1 eller 2 blockerade steg direkt. `tools/stage_output_sequencer.py` översätter detta till högst ett idempotent semantiskt kommando åt gången: `BLOCK_RE1`, `BLOCK_RE2`, `PERMIT_RE2` eller `PERMIT_RE1`. Den väntar på smartmodulens återläsning, sätttid och minsta ändringsintervall innan nästa steg. Därmed blir ordningen alltid `0→1→2` respektive `2→1→0`; rå `relay on/off` och `toggle` förekommer inte i beslutsmodellen.

Efter omstart måste verkligt kontaktorläge vara känt innan smartstyrningen tillåts återlämna ett steg. Benedict-kontaktorernas hjälpkontakter kan eventuellt ge återkoppling, men detta avgörs av elektrikern. Elektrisk invertering och om en kontakt betyder ”blockera” eller ”tillåt” ska endast finnas i utgångsdrivaren, aldrig i pris- eller faspolicyn.

En programvarulease räcker inte om den valda DIN-modulen kan lämna ett relä i blockerande läge när nätverk eller styrenhet försvinner. Den elektriska lösningen behöver därför fail-transparent kontaktlogik, lokal watchdog/auto-release eller tydlig fysisk BYPASS enligt elektrikerns schema.

## Redan testat utan hårdvara

- exakt 5/10/rest-rangordning på ett 96-kvartdygn;
- 92 och 100 verkliga kvartar vid båda tidsomställningarna;
- lika och negativa priser samt blandad indataordning;
- avvisning av luckor, dubbletter, icke ändliga värden och fel tider;
- transparent läge vid saknad plan, BYPASS eller uttryckligen opålitlig tid;
- exakt byte vid kvartgränsen och lease endast till kvartsslutet;
- samspel där pris kan blockera ett steg men fasöverlast kan öka till två;
- att utgången prislease inte tvingar fram återinkoppling utan strömutrymme.
- fail-transparent återgång till enbart COALA när både prisleasen och färsk fasdata saknas;
- kontaktorordning, återläsningskvittens, minsta intervall, timeout/felläge, okänt startläge och BYPASS i den hårdvaruneutrala utgångssekvenseraren.

Firmwaremodulen `main/tibber_price_source.c` är en första, medvetet begränsad shadow-implementation. Den kräver sparat Tibber-hem och NTP-synkroniserad tid, hämtar bara `today`, kontrollerar GraphQL-fel, valuta och ett komplett sammanhängande dygn med 92/96/100 kvartar, rangordnar i RAM/PSRAM och publicerar enbart en läsbar ögonblicksbild. Misslyckade hämtningar återprovas med jitter och exponentiell väntan upp till en timme. Större data läggs i PSRAM men själva task-stacken ligger i internminne eftersom tasken läser NVS-konfiguration. Den sparar ingen plan, hämtar ännu inte morgondagen, visas inte i gränssnittet och kan inte påverka någon utgång. Modulen är avstängd i normal konfiguration. Berörda C-filer har kompilerats med funktionen aktiverad; full länkning på arbetsdatorn stoppades av Windows App Control i verktygskedjans arkiverare och väntar därför på nästa vanliga byggmiljö.

## Återstående arbetsordning

1. Utöka den befintliga dagens shadow-källa med `tomorrow`, atomiskt byte och validerad beständig cache.
2. Visa prisplanen och shadow-besluten på panelen utan någon fysisk utgång.
3. Identifiera de två verkliga effektstegen, `F–K`, kontaktorordning och hjälpkontakter tillsammans med elektrikern.
4. Bestäm komfortgräns, maximal sammanhängande prisblockering och om första versionen endast får använda `Halv`.
5. Implementera och testa kontaktorsekvensering samt fysisk SMART/BYPASS.
6. Kör ett kontrollerat fältprov innan prisstyrningen får påverka patronen.

## Utrymme på panelen

Senast verifierade appbild är 1 597 184 byte i en apppartition på 8 388 608 byte, alltså 6 791 424 byte och cirka 81 procent ledigt. Själva rangordningen och en eller två dygns prisposter är små i förhållande till detta. Flashutrymmet är därför inget praktiskt hinder.

Den verkliga resursfrågan är tillfälligt RAM under HTTPS/JSON. Prisfrågan ska därför vara liten, köras separat från kalender/väder när det går och lägga större svarsbuffertar i PSRAM. Två dygn med högst omkring 200 prisposter är en liten beständig cache; hela GraphQL-svaret behöver inte behållas efter validering.
