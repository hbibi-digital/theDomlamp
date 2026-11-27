# mk1

*English · [Nederlands](README.nl.md)*

The first version of the lamp: the Dom Tower at 1:250 scale in separate printed parts, with a light core of
its own in every section. Licence: CC BY-NC-SA 4.0 (see [`../../LICENSE.md`](../../LICENSE.md)).

**These are the print files of a design in progress, not a kit.** Read "State" first.

## State

- **Printed:** all parts have been printed and the tower stands.
- **Not built as drawn:** the lighting with a light core per section was never finished. A hole was drilled
  in the printed tower afterwards to feed a single LED strip of 52 pixels through it and try things out
  with that. The hole is not in these files. mk2 comes out of that trial: a mast with a single strip.
- **Missing:** the cross and the weather vane have not been drawn.
- **Known weak spot:** in the south face of the first square there is a rib of 0.66 mm between two niches.
- **No print guide and no bill of materials.** Print settings, the orientation of the parts and the
  electronics have not been written down for mk1.

## The parts

From the bottom up. The sizes are the outer dimensions of the file in millimetres.

| File | What | Size (mm) |
| --- | --- | --- |
| `stl/sokkel.stl` | the base under the tower | 96 × 96 × 22 |
| `stl/sokkel_plaat.stl` | the plate in the base | 90 × 90 × 6 |
| `stl/eerste_vierkant.stl` | the first square: the lowest part of the tower | 78 × 78 × 148 |
| `stl/kern_eerste_vierkant.stl` | the light core inside it | 26 × 26 × 104 |
| `stl/glas_in_lood.stl` | insert for the stained-glass window | 10 × 1.4 × 16 |
| `stl/tweede_vierkant.stl` | the second square: the middle part, with the clock dials | 82 × 82 × 120 |
| `stl/kern_tweede_vierkant.stl` | the light core inside it | 26 × 26 × 76 |
| `stl/lantaarn.stl` | the octagonal lantern | 69 × 69 × 132 |
| `stl/kapvloer.stl` | the floor under the roof | 48 × 48 × 7 |
| `stl/dak.stl` | the roof | 43 × 43 × 52 |
| `stl/test_wand.stl` | not part of the tower: a test strip to see how translucent your filament is | 119 × 20 × 15 |

The files have been checked against the model (same outer dimensions, volume within 0.1%) and the meshes
are closed.

## The wiring as drawn

`beelden/` shows how the wiring of mk1 was meant: the whole tower (`bedrading_3d.png`,
`bedrading_context.png`) and the two squares separately. It was not built this way.

## Where the shape comes from

The shape was drawn in code; no model or scan by others went into it. The measurements were read from:

- the survey drawings of the Dom Tower from 1899 (Het Utrechts Archief, public domain);
- the laser scan of the lantern by Erfgoed Utrecht, used as a visual reference;
- our own photographs of the tower.
