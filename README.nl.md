# theDomlamp

*Nederlands · [English](README.md)*

De Domtoren van Utrecht als lamp voor thuis, op schaal 1:250 en uit de 3D-printer. De lamp volgt het licht
van de echte toren: warm wit na zonsondergang, gedimd na elf uur, en een kleur als de Dom een kleur heeft.

De toren zelf, live en in 3D, staat op [utrechtdom.city](https://utrechtdom.city).

## Wat hier staat

| Map | Wat | Staat | Licentie |
| --- | --- | --- | --- |
| [`firmware/`](firmware/) | WLED 16 met de usermod `domlamp`: haalt het lichtplan op en zet de zeven zones van de lamp. | Draait op twee ESP32-C3-borden. | EUPL 1.2 |
| [`hardware/`](hardware/) | Printbestanden, onderdelenlijst en bouwplan. | Volgt. Het ontwerp (mk2) is getekend, nog niet geprint. | CC BY-NC-SA 4.0 |

## Zelf bouwen

Nu kan alleen de firmware: zie [`firmware/README.md`](firmware/README.md) voor bouwen, flashen en de
instellingen. Je hebt een ESP32 of ESP32-C3 nodig en een ledstrip met RGBW-leds (SK6812). De printbestanden
komen in `hardware/` zodra een proefprint is gelukt.

## Licenties, kort

- **Firmware:** [EUPL 1.2](LICENSES/EUPL-1.2.txt), dezelfde licentie als WLED. Gebruiken, aanpassen en
  verkopen mag; wie een aangepaste versie verspreidt, deelt de bron onder dezelfde licentie.
- **Ontwerp van de lamp** (alles in `hardware/`): de nieuwste versie staat onder
  [CC BY-NC-SA 4.0](LICENSES/CC-BY-NC-SA-4.0.txt). Printen, aanpassen en delen mag; verkopen niet.
  Komt er een volgende versie (mk), dan komt de vorige vrij onder
  [CC BY-SA 4.0](LICENSES/CC-BY-SA-4.0.txt) en mag die ook verkocht worden.
- **De naam** theDomlamp en het beeldmerk vallen niet onder deze licenties.

De volledige regeling staat in [`LICENSE.md`](LICENSE.md). Meedoen: [`CONTRIBUTING.md`](CONTRIBUTING.md).

Made by don carrelion in dediepte.
