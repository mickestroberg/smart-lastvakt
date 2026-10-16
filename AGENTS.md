# Arbetsinstruktioner för Smart lastvakt

Detta repo hör till det pågående hemprojektet **Smart lastvakt**. Börja varje ny uppgift med att läsa [PROJECT-STATUS.md](PROJECT-STATUS.md). Läs därefter [README.md](README.md), [HARDWARE-NOTES.md](HARDWARE-NOTES.md) eller [BUILD-REPORT.md](BUILD-REPORT.md) beroende på uppgiften.

## Bestående regler

- Bevara befintliga användarändringar och kontrollera alltid Git-status innan redigering.
- Skilj tydligt mellan verifierad maskinvara, användaruppgifter, beräkningar och ännu oprövade antaganden.
- Lägg aldrig in Wi-Fi-uppgifter, Tibber-token, hemlig kalenderadress, NVS-export eller andra autentiseringsuppgifter i kod, dokumentation, loggar eller Git.
- Simulerade fasvärden får visas, men får aldrig påverka fysisk relästyrning.
- Smartstyrning får endast begränsa last. Den får aldrig tvinga värme, mikrovågsugn eller annan last att starta.
- COALA, pannans termostat och överhettningsskydd ska förbli oberoende säkerhetslager.
- Behandla elpatronen som balanserad trefaslast: mät per fas men reducera kompletta, verifierade trefassteg. Föreslå inte brytning av en ensam fas utan uttryckligt stöd från tillverkaren och elektrikern.
- All fast nätspänningsinstallation, slutligt kopplingsschema och verifiering av kontaktorer ska utföras av auktoriserad elinstallatör.
- Ändra inte reläläge eller flusha ansluten maskinvara enbart som en diagnostisk sidoeffekt. Gör sådana ingrepp när uppgiften kräver dem och målet är tydligt.
- Fabriksbackupen ligger utanför repot och får inte flyttas in i Git.

## Dokumentationsdisciplin

När en uppgift ändrar verifierad funktion, ett viktigt beslut, hårdvarukunskap, säkerhetsregel, mål eller prioritet ska motsvarande dokument uppdateras i samma ändring:

- användning eller mjukvarufunktion → `README.md`;
- samlad status, beslut och nästa steg → `PROJECT-STATUS.md`;
- elektrisk utrustning och styrprincip → `HARDWARE-NOTES.md`;
- verifierad bygg-, flash- eller enhetsstatus → `BUILD-REPORT.md`.

Håll `PROJECT-STATUS.md` användbart som fristående överlämning. Om uppgifterna motsäger äldre dokumentation ska motsägelsen lösas eller markeras uttryckligt; lägg inte bara till ännu en version av sanningen.

## Verifiering

- Bygg firmware efter ändringar som kan påverka kompilering eller länkning.
- Verifiera på den verkliga panelen när en ändring berör display, pekning, nätverk, minne eller hårdvaruintegration och panelen finns tillgänglig.
- Redovisa vad som faktiskt verifierats och vad som återstår.
- Repo och dokumentation ska lämnas fria från hemligheter och oavsiktliga byggprodukter.
