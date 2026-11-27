# mk1

*Nederlands · [English](README.md)*

De eerste versie van de lamp: de Domtoren op schaal 1:250 in losse printdelen, met in elke sectie een eigen
lichtkern. Licentie: CC BY-NC-SA 4.0 (zie [`../../LICENSE.nl.md`](../../LICENSE.nl.md)).

**Dit zijn de printbestanden van een ontwerp in ontwikkeling, geen bouwpakket.** Lees eerst "Staat".

## Staat

- **Geprint:** alle delen zijn geprint en de toren staat.
- **Niet gebouwd zoals getekend:** de verlichting met een lichtkern per sectie is nooit afgebouwd. In de
  geprinte toren is achteraf een gat geboord om één ledstrip van 52 pixels door te voeren en daarmee verder
  te proberen. Dat gat zit niet in deze bestanden. Uit die proef komt mk2: een mast met één strip.
- **Ontbreekt:** het kruis en de windvaan zijn niet getekend.
- **Bekend zwak punt:** in de zuidgevel van het eerste vierkant staat tussen twee nissen een rib van 0,66 mm.
- **Geen printinstructie en geen onderdelenlijst.** Printinstellingen, ligging van de delen en de
  elektronica zijn voor mk1 niet vastgelegd.

## De delen

Van onder naar boven. De maten zijn de buitenmaten van het bestand in millimeters.

| Bestand | Wat | Maat (mm) |
| --- | --- | --- |
| `stl/sokkel.stl` | de voet onder de toren | 96 × 96 × 22 |
| `stl/sokkel_plaat.stl` | de plaat in de voet | 90 × 90 × 6 |
| `stl/eerste_vierkant.stl` | het onderste deel van de toren | 78 × 78 × 148 |
| `stl/kern_eerste_vierkant.stl` | de lichtkern daarin | 26 × 26 × 104 |
| `stl/glas_in_lood.stl` | inzetstuk voor het glas-in-loodvenster | 10 × 1,4 × 16 |
| `stl/tweede_vierkant.stl` | het middelste deel, met de wijzerplaten | 82 × 82 × 120 |
| `stl/kern_tweede_vierkant.stl` | de lichtkern daarin | 26 × 26 × 76 |
| `stl/lantaarn.stl` | de achtkante lantaarn | 69 × 69 × 132 |
| `stl/kapvloer.stl` | de vloer onder het dak | 48 × 48 × 7 |
| `stl/dak.stl` | het dak | 43 × 43 × 52 |
| `stl/test_wand.stl` | geen torendeel: een proefplaatje om te zien hoe doorschijnend je filament is | 119 × 20 × 15 |

De bestanden zijn nagemeten tegen het model (buitenmaat gelijk, inhoud binnen 0,1%) en de meshes zijn
gesloten.

## De bedrading zoals getekend

`beelden/` toont hoe de bedrading voor mk1 bedoeld was: de hele toren (`bedrading_3d.png`,
`bedrading_context.png`) en de twee vierkanten apart. Zo is het niet gebouwd.

## Waar de vorm vandaan komt

De vorm is in code getekend; er is geen model of scan van anderen in verwerkt. De maten zijn afgelezen van:

- de opmetingstekeningen van de Domtoren uit 1899 (Het Utrechts Archief, publiek domein);
- de laserscan van de lantaarn van Erfgoed Utrecht, gebruikt als kijkreferentie;
- eigen foto's van de toren.
