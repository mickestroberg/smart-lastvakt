# Kvartsprisstyrning för elpatronen

Detta dokument beskriver den planerade prisbegränsningen av Relek PF 9 kW och dess samverkan med fasvakten och COALA. Den körbara referensen finns i `tools/quarter_price_policy.py` och testas av `tests/test_quarter_price_policy.py`.

Beslutsreferensen är **inte** inkopplad till någon fysisk utgång. Firmware innehåller nu dessutom en separat, standardmässigt avstängd Tibber-källa som kan hämta och rangordna dagens och morgondagens priser i skrivskyddat shadow mode. En egen panelsida visar besluten som simulering, men källan har ingen beroendekedja till Shelly, COALA eller relä-API. Prisfunktionen ska förbli bortkopplad från verklig styrning tills elektrikern har verifierat `F–K`, godkänt kopplingen och ett säkert reservläge har provats.

## Grundregel

Prislagret anger bara den högsta effekt som smartstyrningen får lämna tillgänglig. Det får aldrig begära att värme startas.

| Prisnivå | Minsta antal blockerade steg | Betydelse |
|---|---:|---|
| Full | 0 | Ingen prisbegränsning; övriga skydd bestämmer |
| Halv | 1 | Högst 50 %; fysisk v1-adapter återstår |
| Av | 2 | Framtida simulering; används inte av säker v1 |

Säker v1 för ett dygn är:

- de **15 dyraste** kvartarna: `Halv`;
- övriga kvartar: `Full`.

Prislagret begär alltså aldrig totalstopp i v1. Patronens huvudmatning ska ligga kvar under prissvängningarna; endast den avsedda halvbegränsningen får användas. `F–K` är den främsta kandidaten eftersom Relek beskriver anslutningen som ingång för extern effektvakt, men elektrikern måste verifiera den verkliga kopplingen och prova att begränsningen både går in och släpper som avsett utan oönskad omstart av tvåtimmarsspärren. `Av` och ett valbart antal 0-procentskvartar får finnas kvar i referensmodellen som framtida konfiguration och simulering, men standardantalet är **0** och nivån får inte anslutas till fysisk prisstyrning i v1.

## Säsongspaus

Prisbegränsningen kan pausas automatiskt under **juni, juli, augusti och september**. Inställningen är påslagen som standard, kan ändras på den lokala installationssidan och sparas i enhetens NVS. Under dessa månader blir prislagrets effektiva beslut `Full`, alltså ingen prisbegränsning. Termostat, COALA, fasvakt och övriga skydd fortsätter naturligtvis att bestämma som vanligt.

Rangordningen körs fortfarande och den råa planen ligger kvar på den skrivskyddade prissidan. Det gör att familjen kan jämföra vilka `50 %`-kvartar som skulle ha valts utan att säsongspausen påverkar någon last. Referensmodellen tillåter även andra eller inga pausmånader, men panelens nuvarande familjeinställning är det enkla valet juni–september på eller av.

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

Priserna grupperas efter lokal kalenderdag i `Europe/Stockholm`, men varje kvart identifieras med sin unika UTC-starttid. Ett normalt dygn har 96 kvartar. Dygnet då sommartid börjar har 92, och dygnet då normaltid återkommer har 100. Kvoten 15 `Halv` behålls; med v1-standardens 0 `Av` blir antalet `Full` alltså 81, 77 respektive 85.

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

Prisfrågan ligger i en separat HTTP-modul och är inte inbyggd i den stabiliserade Pulse-WebSocket-klienten. `today` hämtas efter tids- och nätstart. `tomorrow` efterfrågas med randomiserad fördröjning under eftermiddagen och återprovas med jitter och exponentiell fördröjning tills en komplett plan finns; ett tomt `tomorrow` räknas alltså inte som färdigt dygnsarbete och visas inte som ett familjefel. En senare hämtning ersätter den aktiva prisboken atomiskt först när dagens nya data har validerats; en redan giltig morgondag kan behållas om en mellanliggande hämtning ännu inte innehåller den. Exakt publiceringsklockslag är inte ett villkor för om data godtas.

Källor: [Tibbers GraphQL-schema](https://developer.tibber.com/api/reference.md), [Tibbers API-guide](https://developer.tibber.com/docs/guides/calling-api), [Nord Pools övergång till 15 minuters marknadstid](https://www.nordpoolgroup.com/en/trading/transition-to-15-minute-market-time-unit-mtu/).

## Prisbild, RAM-bok och tidsbegränsade beslut

- Shadow-implementationen behåller validerade planer för i dag och i morgon endast i RAM/PSRAM. En omstart kräver en ny hämtning; ingen prisplan skrivs till NVS eller flash.
- Panelsidan öppnas genom ett tryck på **Tibber Live**-pillret. Den visar prislagrets hypotetiska högsta tillåtna effekt just nu och listar de kommande 24 timmarnas begränsade kvartar med totalpris och rang inom respektive dygn. Med v1-standard visas bara `50 %`; `0 %` kan visas endast om framtida simuleringskonfiguration uttryckligen aktiveras.
- Om morgondagens priser ännu inte har publicerats visas bara den tillgängliga delen fram till midnatt med ett neutralt informationsmeddelande. Detta är normal partiell täckning, inte ett synkfel.
- `50 %` och `Full` är högsta tillåtna nivå från just prislagret, inte ett löfte om att patronen kommer att gå. Termostat och överordnade skydd kan alltid begränsa mer. Ett eventuellt simulerat `0 %` är inte en tillåten fysisk v1-order.
- Den aktiva kvarten är ett halvöppet intervall `[start, slut)`, så exakt kvartsskifte väljer den nya kvarten utan glapp.
- En begränsning översätts till en kort monoton lease, normalt högst 60 sekunder och aldrig längre än den aktuella kvartens ursprungliga sluttid. Ett fungerande kontrollflöde förnyar den medan samma begränsning gäller.
- Ett nät- eller API-fel får inte förlänga leasen.
- Vid omstart krävs en ny validerad RAM-plan och trovärdig väggklocka; annars är prislagret transparent.
- Prisleasen lagras inte som permanent relätillstånd.

Referens-API:t har en uttrycklig `time_trusted`-grind. `app_time` behöver före firmwareintegrationen exponera tidskvalitet, exempelvis okänd byggtid, RTC och NTP, och mata denna grind. NTP är bäst. Om RTC ensam ska få aktivera en sparad prisplan är ett separat beslut; en tidszonsmedveten men felställd klocka får inte betraktas som trovärdig bara på grund av sitt format.

## Reläsekvensering och återkoppling

Den gemensamma lastreferensen kan fortfarande modellera 0, 1 eller 2 blockerade steg för framtida fas- och kontaktorarbete, och `tools/stage_output_sequencer.py` kan prova säker ordning utan hårdvara. **Prislagret i v1 begär däremot bara 0 eller 1 blockerat steg.** Det får aldrig begära huvudströmsbrytning eller ett andra steg. En framtida fysisk prisadapter ska använda ett verifierat semantiskt kommando för `F–K`/halvbegränsning, inte rå `relay on/off` eller `toggle`.

Efter omstart måste verkligt kontaktorläge vara känt innan smartstyrningen tillåts återlämna ett steg. Benedict-kontaktorernas hjälpkontakter kan eventuellt ge återkoppling, men detta avgörs av elektrikern. Elektrisk invertering och om en kontakt betyder ”blockera” eller ”tillåt” ska endast finnas i utgångsdrivaren, aldrig i pris- eller faspolicyn.

En programvarulease räcker inte om den valda DIN-modulen kan lämna ett relä i blockerande läge när nätverk eller styrenhet försvinner. Den elektriska lösningen behöver därför fail-transparent kontaktlogik, lokal watchdog/auto-release eller tydlig fysisk BYPASS enligt elektrikerns schema.

## Redan testat utan hårdvara

- exakt 0/15/rest-rangordning på ett 96-kvartdygn;
- 92 och 100 verkliga kvartar vid båda tidsomställningarna;
- lika och negativa priser samt blandad indataordning;
- avvisning av luckor, dubbletter, icke ändliga värden och fel tider;
- transparent läge vid saknad plan, BYPASS eller uttryckligen opålitlig tid;
- standardpaus juni–september, valfri avstängning av pausen och fortsatt synlig råplan;
- exakt byte vid kvartgränsen och lease endast till kvartsslutet;
- samspel där pris endast kan blockera ett steg medan den separata fasreferensen fortfarande kan modellera strängare begränsning;
- att utgången prislease inte tvingar fram återinkoppling utan strömutrymme.
- fail-transparent återgång till enbart COALA när både prisleasen och färsk fasdata saknas;
- kontaktorordning, återläsningskvittens, minsta intervall, timeout/felläge, okänt startläge och BYPASS i den hårdvaruneutrala utgångssekvenseraren.

Den samlade datorbaserade sviten omfattar nu **66 tester**, inklusive v1-standardens 0/15-fördelning och de nya säsongsfallen.

Firmwaremodulen `main/tibber_price_source.c` är en medvetet begränsad shadow-implementation. Den kräver sparat Tibber-hem och NTP-synkroniserad tid, frågar efter `today` och `tomorrow`, kontrollerar GraphQL-fel, valuta och varje helt sammanhängande dygn med 92/96/100 kvartar och publicerar en atomisk, läsbar prisbok i RAM/PSRAM. Misslyckade hämtningar återprovas med jitter och exponentiell väntan upp till en timme utan att en fortfarande giltig bok behöver kastas. Större data läggs i PSRAM men själva task-stacken ligger i internminne eftersom tasken läser NVS-konfiguration. Modulen är avstängd i normal konfiguration, sparar ingen plan beständigt och kan inte påverka någon utgång.

Den skrivskyddade sidan i `main/ui.c` visar aktuell hypotetisk prisgräns och paginerar endast de begränsade kvartarna inom de kommande 24 timmarna. Standardplanen innehåller 15 `50 %`-kvartar och inga `0 %`-kvartar. Under säsongspausen markeras beslutet som sommarpaus samtidigt som råplanen fortsätter synas. Efter v1-omläggningen kompilerade prisvägens berörda C-enheter separat med shadow-flaggan på och den lokala 0/15-konfigurationen; den villkorsstyrda UI-koden hade tidigare även verifierats med flaggan av. Full arkivering och länkning på arbetsdatorn stoppas fortfarande av Windows App Control i verktygskedjans arkiverare med felkod 4551. Den nya prisvyn och v1-regeln har inte flashats eller fältprovats på panelen.

## Återstående arbetsordning

1. Fullbygg, flasha och observera shadow-läget under flera dygn utan någon fysisk utgång. Kontrollera särskilt dygnsskifte, sen publicering av morgondagen och sommarpausens råplan.
2. Avgör efter fältprovet om en beständig pris-cache verkligen behövs; nuvarande prisbok är avsiktligt RAM-baserad och hämtas på nytt efter omstart.
3. Låt elektrikern verifiera `F–K`, inklusive att patronen kan hållas spänningssatt och växla mellan normalt och högst halv effekt utan oönskad samverkan med tvåtimmarsspärren.
4. Rita och godkänn en fail-transparent fysisk adapter och SMART/BYPASS för just halvbegränsningen. Prislagret ska inte få en fysisk totalstoppsutgång i v1.
5. Kör ett kontrollerat fältprov med mätning av ström per fas innan prisstyrningen får påverka patronen.

## Utrymme på panelen

Senast verifierade appbild är 1 597 184 byte i en apppartition på 8 388 608 byte, alltså 6 791 424 byte och cirka 81 procent ledigt. Själva rangordningen och en eller två dygns prisposter är små i förhållande till detta. Flashutrymmet är därför inget praktiskt hinder.

Den verkliga resursfrågan är tillfälligt RAM under HTTPS/JSON. Prisfrågan ska därför vara liten, köras separat från kalender/väder när det går och lägga större svarsbuffertar i PSRAM. Två dygn med högst omkring 200 prisposter är en liten RAM-bok; hela GraphQL-svaret behöver inte behållas efter validering.
