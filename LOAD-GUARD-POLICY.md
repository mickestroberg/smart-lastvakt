# Referensmodell för framtida laststyrning

`tools/load_guard_policy.py` är en körbar och automatiskt testad specifikation för den framtida tvåstegsstyrningen. Den ingår **inte** i ESP32-firmware, öppnar inga nätverksanslutningar och har inga fysiska utgångar.

Syftet är att låsa säkerhetsbeteendet innan kontaktorer, Relek-modell och verkliga effektsteg är identifierade. När hårdvaran är verifierad kan samma tillståndsmaskin porteras till firmware med de verkliga parametrarna.

## Modellens ingångar

- L1, L2 och L3 samt mätvärdets ålder.
- Markering för saknat eller simulerat värde.
- Monoton tid för fördröjningar och minsta tid mellan ändringar.
- En tidsbegränsad extern begäran som exempelvis kan komma från prisstyrning eller ett manuellt stopp.

Den externa begäran kan bara öka antalet blockerade steg. Den kan aldrig ge tillstånd att starta ett steg.

## Stegordning

- Steg 1 motsvarar COALA `Re1`: kopplas bort först och återansluts sist.
- Steg 2 motsvarar COALA `Re2`: kopplas bort därefter och återansluts först.
- Ett steg representerar alltid ett komplett, verifierat och balanserat trefassteg.

## Färskhet och felbeteende

Endast tillgängliga, icke-simulerade, ändliga och tillräckligt färska fasvärden får användas för fasbeslut. Saknade, simulerade, gamla eller ogiltiga värden gör faslogiken transparent så att COALA kan ta över. En ännu giltig tidsbegränsad extern blockering kan ligga kvar tills dess sluttid.

## Frånkoppling och återinkoppling

- Den högsta fasströmmen jämförs med en konfigurerad gräns.
- Varje steg har egen frånkopplingsfördröjning.
- Fortsatt överlast kan blockera nästa steg.
- Återinkoppling sker bara om uppmätt maxström plus det återvändande stegets förväntade fasström ryms under gränsen med konfigurerad marginal.
- Återinkopplingsvillkoret måste vara stabilt under en konfigurerad tid.
- En minsta tid mellan ändringar förhindrar kontaktorpendling.

## Viktigt om exempelvärdena

Funktionen `pf_candidate_example_config()` använder 23 A som illustrativ gräns och 6,5 A per steg. Typskylten bekräftar nu att patronen är Relek PF 9 kW med tvåtimmarsspärr; 6,5 A motsvarar beräknad fasström för halva märkeffekten. Det är däremot ännu inte verifierat att de två externa styrstegen motsvarar två lika stora 4,5 kW-steg. Fördröjningarna är också demonstrationsvärden. **Inget av detta är godkända installationsinställningar.** Verkliga steg, strömmar och gränser bestäms efter mätning och kontroll av elektrikern.

## Köra tester och demonstration

Från repots rot:

```powershell
python -m unittest discover -s tests -v
python -m tools.simulate_load_guard
```

Testerna täcker bland annat högsta fas, stegordning, fördröjning, beräknat utrymme före återinkoppling, simulerad och gammal data samt utgången tidsbegränsad blockering.

## Aktiv mikrovågsvakt

Mikrons enklare, ensidiga skydd finns i firmware i `main/microwave_guard.c` och som datorbaserad beslutsmodell i `tools/microwave_guard_policy.py`. Modellen testas av `tests/test_microwave_guard_policy.py`.

Standardregeln begär Shelly **AV** när samtliga villkor är uppfyllda:

- färsk, verklig Tibberdata finns och är högst 5 sekunder gammal;
- Shellyn är nåbar, påslagen och inget annat reläkommando pågår;
- Shellyn mäter minst 500 W, vilket betyder att mikron bedöms vara aktiv;
- total L1 är strikt över 20,0 A.

Exakt 20,0 A utlöser inte frånkoppling. Simulerad, gammal eller saknad fasdata kan aldrig ge ett reläkommando. Vakten innehåller ingen väg som begär **PÅ**; återinkoppling är en separat manuell åtgärd i panelens relävy.
