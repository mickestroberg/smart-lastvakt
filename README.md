# Smart lastvakt

En svensk klocka och fasvakt för **Waveshare ESP32-S3-Touch-LCD-4.3B-BOX** (800 × 480, ESP32-S3-N16R8). Projektet visar Tibber Pulse-strömmarna för L1, L2 och L3 och växlar automatiskt till fasvyn när någon fas passerar 50 % av huvudsäkringen.

Standardvärdet för huvudsäkringen är **25 A**.

![Förhandsvisning av gränssnittet](ui-preview.svg)

## Funktioner

- Stor klocka, svenskt datum och automatisk CET/CEST.
- Kortets batteribackade PCF85063A används vid start; NTP rättar tiden efter Wi-Fi-anslutning.
- Tibber GraphQL-abonnemang med `currentL1`, `currentL2` och `currentL3` i realtid.
- Simulerade fasvärden används automatiskt tills Tibber är konfigurerat eller om liveflödet är tillfälligt nere.
- Automatisk fasvy vid 50 % belastning och återgång efter åtta sekunder.
- Manuell växling mellan klocka och fasvy via pekskärmen.
- Lokal installationssida för SSID, Wi-Fi-lösenord och Tibber Personal Access Token.

## Första starten

1. Starta displayen och anslut en mobil eller dator till nätverket `Lastvakt-Setup-XXXX`.
2. Öppna `http://192.168.4.1`.
3. Fyll i Wi-Fi-namn, Wi-Fi-lösenord och din Tibber Personal Access Token.
4. Tryck **Spara och starta om**.

Efter en lyckad anslutning visar displayen adressen där samma installationssida kan nås på det lokala nätverket. Om Wi-Fi-anslutningen misslyckas startas installationsnätverket igen.

En personlig token kan skapas på [Tibber Developer](https://developer.tibber.com/). Firmware väljer automatiskt det första hem där realtidsmätning är aktiverad och sparar hem-id lokalt.

### Sekretess och säkerhet

SSID, lösenord och token lagras i ESP32-enhetens NVS-minne och skrivs aldrig till projektfiler eller loggar. Installationssidan använder vanlig lokal HTTP och saknar egen inloggning; använd den därför bara på ett betrott hemnätverk och låt inte installationsnätverket vara åtkomligt längre än nödvändigt.

## Bygga

Projektet använder ESP-IDF 5.5.5. Hanterade komponenter hämtas automatiskt vid första byggningen.

```powershell
idf.py set-target esp32s3
idf.py build
```

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
- belastningsgräns för automatisk fasvy, standard 50 %;
- hur länge fasvyn ligger kvar efter varningen, standard 8 sekunder.

## Struktur

- `main/network_setup.c` – Wi-Fi, NVS och den lokala installationssidan.
- `main/tibber_client.c` – Tibbers GraphQL- och WebSocket-klient.
- `main/phase_source.c` – väljer livevärden eller simulering.
- `main/app_time.c` – RTC, NTP och svensk tidszon.
- `main/ui.c` – LVGL-gränssnittet och vyväxlingen.

Kort- och bildskärmsinitieringen bygger på Waveshares officiella LVGL v8-exempel för kortet. Se [BUILD-REPORT.md](BUILD-REPORT.md) för den provade bygg- och enhetsstatusen.
