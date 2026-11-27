# Firmware van de Domlamp

De lamp draait gewone WLED 16.0.1 met één eigen usermod: `domlamp`.
De usermod haalt elke minuut het lichtplan op (`https://utrechtdom.city/api/licht`) en zet de zeven zones van de lamp.
Al het andere is WLED zelf: wifi instellen, de webpagina, updates, de stroomgrens.

De firmware draait op twee ESP32-C3-borden (sinds 25 november 2026). Wat wel en niet is getest staat onderaan.

## Wat er in deze map staat

| Pad | Wat |
| --- | --- |
| `usermod_domlamp/usermod_domlamp.cpp` | De WLED-kant: segmenten, instellingen, infopagina. |
| `usermod_domlamp/domlamp_core.*` | De beslissingen: plan lezen, scène kiezen, kaars, terugval. Gewoon C++, zonder Arduino. |
| `usermod_domlamp/domlamp_fetch.*` | Ophalen over HTTPS, in een eigen taak. |
| `usermod_domlamp/domlamp_link.*` | Koppelen via de server: de open verbinding naar de doorgeefdienst, in een eigen taak. |
| `usermod_domlamp/domlamp_net.h` | De TLS-bibliotheek en haar instellingen, gedeeld door ophalen en koppelen. |
| `usermod_domlamp/domlamp_certs.h` | De twee rootcertificaten van Let's Encrypt. |
| `usermod_domlamp/library.json` | Maakt van de map een WLED-usermod (zo wil WLED 16 het). |
| `platformio_override.ini` | De bouwomgevingen `domlamp_esp32` en `domlamp_esp32c3`. |
| `build.sh` | Haalt WLED op (buiten deze repo) en bouwt. |
| `test/` | Test van de kern op de Mac, zonder hardware. |

## Bouwen

Nodig: git, node/npm en PlatformIO (`uv tool install platformio`).

```sh
./build.sh                    # ESP32, 4 MB
./build.sh domlamp_esp32c3    # ESP32-C3 SuperMini, data op GPIO 10
```

Het script kloont WLED `v16.0.1` naar `~/.cache/domlamp-fw/WLED` (of naar `$DOMLAMP_WLED_DIR`).
Het legt een symlink `usermods/domlamp` naar deze map en kopieert `platformio_override.ini`.
De firmware komt in `build/<omgeving>.bin`. Die map staat in `.gitignore`.

## Flashen

- **Eerste keer, via USB:** `./build.sh domlamp_esp32 -t upload --upload-port /dev/cu.usbserial-XXXX`.
  Dat zet ook de bootloader en de partitietabel erop.
- **Daarna, of op een bord waar al WLED op staat:** in WLED naar *Config → Security & Updates → Manual OTA Update* en `build/domlamp_esp32.bin` kiezen.
  De instellingen blijven dan staan.
- De Dig2Go heeft al WLED. Of een update vanaf díe versie naar 16.0.1 zonder wissen goed gaat is niet geprobeerd.

## Eerste instellingen in WLED

1. Wifi: de lamp start als toegangspunt `WLED-AP`. Kies daar je netwerk.
2. *Config → LED Preferences*: type SK6812 RGBW, het aantal pixels, de datapin.
   De bouw zet als begin: SK6812 RGBW, 52 pixels, stroomgrens 2500 mA.
   Datapin: GPIO 16 op de ESP32-bouw (de standaard van WLED), GPIO 10 op de C3-bouw.
   Zet *Auto-calculate W channel from RGB* (en *Auto-calculate white* bij de strip) op **None**. Het plan stuurt het wit zelf.
3. *Config → Time & Macros*: NTP staat in deze bouw al aan, tijdzone Midden-Europa. Controleer of de klok klopt.
4. Laat de overgangsstijl van WLED (blending) op *Fade* staan. Met een andere stijl ziet de kaars er anders uit.

## Segmenten: welke pixel bij welke zone

De strip loopt aan één kant omhoog en aan de andere kant omlaag. Elke hoogte heeft dus twee pixels.
Daarom krijgt elke zone standaard **twee segmenten**:

| Zone | Segment omhoog | Segment omlaag |
| --- | --- | --- |
| voet | 0 | 13 |
| eerste_vierkant | 1 | 12 |
| omloop1 | 2 | 11 |
| tweede_vierkant | 3 | 10 |
| omloop2 | 4 | 9 |
| lantaarn | 5 | 8 |
| dak | 6 | 7 |

- De usermod zet alleen de **eerste kleur** en de **helderheid** (opacity) van die segmenten.
  Welke pixels in een segment zitten bepaal jij in WLED.
- Heeft WLED nog maar één segment, dan maakt de usermod er bij het opstarten veertien, gelijk verdeeld. Dat is alleen een begin.
- Tel daarna op de echte strip en zet per segment *Start* en *Stop*.
  Een pixel ter hoogte van een tussenschot laat je buiten elk segment. Die blijft uit.
- **Bewaar de segmenten als preset en kies die preset bij *Apply preset at boot*.**
  WLED onthoudt segmenten alleen in presets. Zonder dat zijn ze na een herstart weg.
- Bestaat een segment niet, dan slaat de usermod het over. Er gaat niets stuk.
- Liever zeven segmenten? Zet *two-segments-per-zone* uit. Dan hoort zone 1–7 bij segment 0–6.
  De twee kanten breng je dan samen met een `ledmap.json` van WLED.

De volgorde omhoog/omlaag is een aanname. Loopt jouw strip anders, verwissel dan de bereiken van de segmenten.

## Instellingen van de usermod

*Config → Usermods → Domlamp.*

| Veld | Standaard | Betekenis |
| --- | --- | --- |
| `enabled` | aan | De Domtoren volgen. |
| `url` | `https://utrechtdom.city/api/licht` | Waar het plan staat. `http://` mag ook (voor een proef in huis). `https` alleen op poort 443. |
| `interval` | 0 | Seconden tussen twee keer ophalen. 0 = wat de server vraagt (`volgende_s`, nu 60). |
| `verify-certificate` | aan | Het certificaat van de server controleren. |
| `first-segment` | 0 | Het segment van de voet. |
| `two-segments-per-zone` | aan | Zie hierboven. |
| `create-segments` | aan | Segmenten maken als WLED er maar één heeft. |
| `fallback-on` / `fallback-off` | 17:00 / 23:00 | Warm wit zonder plan, lokale tijd. Mag over middernacht. Gelijke tijden = altijd aan. |
| `fallback-level` | 255 | Helderheid van dat warme wit. |
| `store-plan-hours` | 6 | Hoe vaak het plan hoogstens naar flash gaat. 0 = nooit. |

## Zelf aan de lamp zitten

- **De hoofdhelderheid en de aan/uit-knop van WLED zijn van jou.** De usermod komt daar niet aan. Dimmen kan dus altijd.
- **De usermod schrijft alleen als de scène verandert.** Niet elke minuut.
  Verander je met de hand een kleur, dan blijft die staan tot de volgende scènewissel van de toren. Dat zijn er een paar per dag.
- **Wil je dat het blijft staan, zet het volgen dan uit.** Dat kan op drie manieren:
  - de knop bij *Dom tower* op de infopagina;
  - het vinkje `enabled` in de instellingen (dat is ook de stand na een herstart);
  - in een preset of via de JSON-API: `{"Domlamp":{"enabled":false}}`, en `true` om weer te volgen.
- Zet je het volgen weer aan, dan toont de lamp meteen de scène van dat moment.
- Een preset die je bewaart onthoudt ook of het volgen aan stond.

## Koppelen via de server (proef)

Gebouwd en op de tweede lamp gemeten op 26 november 2026; de dienst draait live (zie "Niet getest" voor wat openstaat).

Met de instelling `link-url` (leeg = uit; bedoeld: `https://utrechtdom.city/api/koppel`) houdt de lamp één
verbinding open naar de doorgeefdienst (`web/deploy/koppel/koppel.py`). Een browser die de code van de lamp
kent, zet daar zijn zones neer; de lamp toont ze met een overgang van 120 ms en laat zijn segmenten 12 s
met rust na het laatste beeld, net als bij de directe koppeling. Daarna volgt hij weer de Domtoren.

- De code (`link-code`, acht tekens) maakt de lamp zelf bij het eerste gebruik en staat op de infopagina
  bij "Dom link code", met de toestand van de verbinding en het aantal beelden.
- De verbinding loopt in een eigen taak (`domlamp_link.cpp`), met dezelfde TLS-bibliotheek en
  certificaatcontrole als het ophalen van het plan (`domlamp_net.h`). Over https houdt hij naar schatting
  45 kB geheugen vast zolang hij open staat; hij begint er niet aan onder 70 kB vrij.
- Valt de verbinding weg, dan probeert de lamp het opnieuw na 5 s, oplopend tot 60 s.
- De lamp vraagt de stroom op met HTTP/1.0, zodat het antwoord niet in stukken (chunks) komt.

## Overgangen

- `vervagen`: alle zones tegelijk, in `duur_s` seconden.
- `kaars_aan`: zone na zone, van het dak naar de voet. Elke zone krijgt een zevende van de tijd.
- `kaars_uit`: zone na zone, van de voet naar het dak.
- De overgang uit het plan geldt alleen als de lamp de wissel binnen 90 seconden meemaakt.
  Start de lamp later op, dan vervaagt hij in 2 seconden naar de juiste scène.

## Zonder internet

- De lamp volgt het laatste plan op zijn eigen klok. Een plan loopt 36 uur vooruit.
- Het plan gaat hoogstens eens per 6 uur naar flash (ongeveer 0,6 kB, vier keer per dag). Dat slijt het geheugen niet merkbaar.
  Na een herstart gebruikt de lamp dat plan, **als hij de tijd weet**. Zonder internet en zonder klok kan dat niet.
- Is het plan op, of was er nooit een: warm wit (alleen het W-kanaal) tussen `fallback-on` en `fallback-off`.
- Weet de lamp de tijd niet, dan staat hij warm wit **aan**. Een lamp die donker blijft lijkt kapot.
- Na een mislukte poging wacht de lamp steeds twee keer zo lang, tot hoogstens 15 minuten.

## De klok

- De lamp gebruikt de klok van WLED (NTP).
- Bij elk plan vergelijkt hij die met `tijd` van de server. Meer dan 2 minuten verschil staat op de infopagina (*Dom clock*).
  De lamp blijft dan zijn eigen klok volgen. Een verschil kan ook betekenen dat de server het plan niet meer ververst.
- Heeft WLED geen klok, dan neemt de lamp `tijd` van de server over. Dat kan alleen als `verify-certificate` uit staat of de URL `http://` is: voor de controle van het certificaat is de datum nodig.

## Certificaat

WLED 16 wordt gebouwd op een framework waarin TLS is uitgezet. Daarom zit er een eigen TLS-bibliotheek bij: BearSSL, via `ESP_SSLClient` 3.1.3.

- **Keuze: het certificaat wordt gecontroleerd**, tegen de twee roots van Let's Encrypt (ISRG Root X1 en X2). Die zitten in de firmware (2,7 kB).
- **Het risico:** stapt de server over op een andere uitgever, dan weigert de lamp het plan. Caddy kan dat zelf doen (terugval op ZeroSSL).
  Je ziet dan een fout op de infopagina en de lamp valt terug op het oude plan en daarna op warm wit.
  Oplossing: `verify-certificate` uitzetten, of nieuwe firmware met de juiste root.
- Staat de controle uit, dan is de verbinding wel versleuteld maar kan iemand op het netwerk zich voordoen als de server.
- De roots verlopen in 2035 en 2040.
- Kosten, gemeten in de bouw: zie *Maten*. Tijdens het ophalen kost het ongeveer 45 kB werkgeheugen, een paar seconden per minuut. Dat laatste is geschat, niet gemeten.

## Infopagina

Onder *Info* in WLED:

- *Dom tower*: de knop voor volgen aan/uit.
- *Dom scene*: de scène nu, en waar die vandaan komt (plan, bewaard plan, terugval).
- *Dom next*: de volgende wissel.
- *Dom last fetch*: hoe lang geleden, en hoe lang het duurde.
- *Dom error*: de laatste fout, als die er is.
- *Dom clock*: alleen als de klok meer dan 2 minuten afwijkt.
- *Dom fetch stack*: hoeveel stack de ophaaltaak overhield. Handig bij de eerste proef op hardware.

## Maten (uit de bouw, WLED 16.0.1, 4 MB flash, app-partitie 1 572 864 bytes)

| Bouw | Flash gebruikt | Vrij | RAM (statisch) |
| --- | --- | --- | --- |
| WLED kaal, ESP32 (`esp32dev`) | 1 297 849 (82,5 %) | 275 015 | 81 552 |
| Domlamp, ESP32 | 1 407 089 (89,5 %) | 165 775 | 85 632 |
| WLED kaal, ESP32-C3 (`esp32c3dev`) | 1 264 840 (80,4 %) | 308 024 | 71 768 |
| Domlamp, ESP32-C3 | 1 375 494 (87,5 %) | 197 370 | 75 848 |

De usermod met TLS kost dus ongeveer 110 kB flash en 4 kB vast RAM.
"RAM" is hier wat de linker vastlegt, niet het vrije geheugen op een draaiende lamp. Dat is niet gemeten.

## Getest

**Op de Mac (`test/run.sh`, 514 controles, met AddressSanitizer en UBSan):**

- het plan lezen, ook het echte voorbeeld en een echt antwoord van de server (chunked);
- afgekapte en foute antwoorden: geweigerd, geen crash;
- welke scène wanneer geldt, en wanneer het plan op is;
- de kaars, vergeleken met `candle()` uit de web-app;
- de terugval-tijden, ook over middernacht;
- wachttijden na fouten, URL's lezen;
- het plan in- en uitpakken voor flash, met beschadigde bytes.

**Gecompileerd:** beide omgevingen bouwen zonder fouten en zonder waarschuwingen in de eigen code.

**Los gecontroleerd op de Mac:** BearSSL (de gewone versie, niet de kopie in de bibliotheek) maakt verbinding met de echte server en keurt het certificaat goed met alleen deze twee roots. Met een andere root weigert het.

## Niet getest

- **Koppelen via de server, meer dan één lamp en langer dan vijf minuten:** op de tweede lamp (ESP32-C3)
  gemeten op 26 november 2026. Met de dienst op de Mac over http: 30 van 30 beelden, post tot zichtbaar
  mediaan 48 ms (één rondje uitlezen is 47 ms, de echte vertraging ligt daaronder). Met de live dienst over
  https (Caddy, nginx): 40 van 40 beelden, mediaan 78 ms, traagste 151 ms, bij een uitleesrondje van 53 ms;
  rechtstreeks naar de lamp op dezelfde manier 81 ms. Vrij geheugen: 150 kB zonder verbinding, 134 kB met
  http, 111 kB met https, en elke minuut een paar seconden 57 tot 62 kB terwijl het plan wordt opgehaald.
  Het plan kwam er in vijf minuten elke keer door. Eén herstart gezien, direct na het opslaan van een
  instelling via `/json/cfg` terwijl het geheugen op 58 kB stond; oorzaak niet vastgesteld. Niet getest:
  uren achter elkaar, een wegvallende verbinding, meer lampen tegelijk.

Alles wat een ESP32 nodig heeft:

- of de TLS-verbinding op de chip lukt, hoe lang die duurt en of de stack (12 kB) genoeg is;
- of het licht hapert tijdens het ophalen, vooral op de C3 met één kern;
- vrij geheugen op een draaiende lamp;
- de segmenten: kleur, helderheid, overgangen, de kaars op echte pixels;
- de standaardsegmenten en de samenloop met een opstartpreset;
- instellingen, infopagina en de aan/uit-knop in de webpagina;
- het plan bewaren en teruglezen op het bestandssysteem;
- gedrag bij een update over wifi;
- de kleuren zelf (de RGBW-waarden uit het plan zijn op de server ook nog een aanname).

## Eigen naam en uiterlijk

- **In de build** (`platformio_override.ini`): merk "Domlamp", product "Domtoren", adres `domlamp.local`, en
  een instelnetwerk met eigen naam per lamp (`Domlamp-` plus de laatste zes tekens van het serienummer).
- **Op de lamp** (`ui/install.sh <adres>`): `skin.css` met de kleuren van de site voor WLED's eigen pagina's,
  en vier presets: Volg de Domtoren, Warm wit, Oranje, Uit. Het script overschrijft de presets op de lamp.
- Niet aangepast: het WLED-logo, de welkomstpagina en de menu's. Dat vraagt wijzigingen in WLED zelf.
- Gezien op de lamp (25 nov 2026): naam, merk, stijlblad en presets staan erop; de presets schakelen het
  volgen aan en uit. De kleuren van de presets en het uiterlijk in de browser zijn niet beoordeeld.
