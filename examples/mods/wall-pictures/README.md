# Wall Pictures

Repaints every painting on the hallway's walls with one PNG: the paintings all share one texture, an atlas,
`/Assets/sh/environ/object/shsb/house/shsb_hous001/sourceimages/shsb_hous001_a1_bsm.ftex`. A `.png` at the same path
(with `.png` in place of `.ftex`) under the mod's `Assets/` folder replaces it.

This example's picture is a generated pattern, so each painting shows a part of it. To repaint the real paintings, get the
original with `python tools/ftex.py --path /Assets/sh/environ/object/shsb/house/shsb_hous001/sourceimages/shsb_hous001_a1_bsm.ftex --out <dir>`,
paint over it (keep its layout: every painting is one rectangle of it) and save it here under the same name.
