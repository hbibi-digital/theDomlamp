# theDomlamp

*English · [Nederlands](README.nl.md)*

The Dom Tower of Utrecht as a lamp for your home, at 1:250 scale and off the 3D printer. The lamp follows
the lighting of the real tower: warm white after sunset, dimmed after eleven, and a colour when the Dom
wears one.

The tower itself, live and in 3D, is at [utrechtdom.city](https://utrechtdom.city).

## What is here

| Folder | What | State | Licence |
| --- | --- | --- | --- |
| [`firmware/`](firmware/) | WLED 16 with the `domlamp` usermod: fetches the lighting plan and sets the lamp's seven zones. | Runs on two ESP32-C3 boards. | EUPL 1.2 |
| [`hardware/`](hardware/) | The lamp's design. [mk1](hardware/mk1/): print files (STL) of a design in progress, printed but never finished as drawn. mk2: drawn, not printed yet. | No build guide yet. | CC BY-NC-SA 4.0 |

The documentation inside the folders is in Dutch; code, comments and commit messages are in English.

## Build it yourself

The firmware is ready to use: see [`firmware/README.md`](firmware/README.md) (Dutch) for building, flashing
and the settings. You need an ESP32 or ESP32-C3 and an RGBW LED strip (SK6812). The print files of mk1 are
in [`hardware/mk1/`](hardware/mk1/), without a build guide; those of mk2 follow once a test print has succeeded.

## Licences, in short

- **Firmware:** [EUPL 1.2](LICENSES/EUPL-1.2.txt), the same licence as WLED. You may use, change and sell
  it; if you distribute a changed version, you share its source under the same licence.
- **The lamp's design** (everything in `hardware/`): the newest version is under
  [CC BY-NC-SA 4.0](LICENSES/CC-BY-NC-SA-4.0.txt). You may print it, change it and share your version;
  you may not sell it. When a next version (mk) comes out, the one before it is released under
  [CC BY-SA 4.0](LICENSES/CC-BY-SA-4.0.txt) and may be sold as well.
- **The name** theDomlamp and its logo are not covered by these licences.

The full terms are in [`LICENSE.md`](LICENSE.md) (Dutch). Taking part: [`CONTRIBUTING.md`](CONTRIBUTING.md)
(Dutch).

Made by don carrelion in dediepte.
