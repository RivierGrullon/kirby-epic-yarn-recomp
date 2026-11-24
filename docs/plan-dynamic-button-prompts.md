# Dynamic button prompts

Kirby's Epic Yarn draws the same Wii button in several ways. The cutscene Skip
bar uses a standalone color map with a shared mask, while menus also print
buttons as characters inside text. Replacing the dialogue icon alone leaves
these other prompts showing their original symbols.

## Input and settings

The launcher and game share the saved controller and keyboard bindings. Enable
**Use controller and keyboard icons** under **Advanced settings → Controls →
Button bindings**. Choose a fixed family or **Last used device**. Settings take
effect on game launch. In automatic mode a mapped player-one input changes the
displayed family during play.

| Wii prompt | Game action | Default gamepad position | Default key |
| --- | --- | --- | --- |
| 2 | Jump / Confirm | South | Space |
| 1 | Yarn / Cancel | East | Left Ctrl |
| A / B | Wii A / Wii B | North / West | X / Z |
| + | Pause / Plus, including Skip | Start / Options | Escape |
| − | Map / Minus | Back / Create | Tab |
| HOME | Home | Guide | F1 |
| Direction | Move in the indicated direction | Corresponding D-pad direction | Corresponding arrow |

## RK5E01 texture coverage

| Scene or resource | How the game draws the prompt |
| --- | --- |
| Cutscenes, mini-help bars and dialogues | Standalone color maps in MiniHelp2, NextButton and sync_info, some with a shared circular mask |
| Menus and translated messages | U+2460, U+2461 and U+2462 in GameFont1 and GameFont2 represent Wii 1, 2 and A |
| Door entry, Patch Castle signs and the train tutorial | The door speech bubble and animated RGB5A3 textures in z100_tutorial00–13 and z301_train00 |
| Controls and transformation guides | PlayGuideWindow color maps and individual cells in two button atlases with intensity maps |
| HOME menu | The controller legend and HOME key overlay |

The catalog in `runtime/src/graphics/button_prompt_catalog.inc` identifies each
source by its base-level GX hash, dimensions and format. Its rectangles use the
source texture's coordinates. For example, the dialogue Wii 2 color map is
`tex1_48x48_666f92b6572aeeb2_14`. MiniHelp2 uses the separate 32×32
`tex1_32x32_410a08d7ebac91cd_14` for Wii +.

## Extending coverage

1. Identify the on-screen action and trace its layout, font cell or animated
   model texture in the relevant disc revision. The NSMBWiicompiled reference
   demonstrates the technique for fonts and standalone layout icons. Kirby
   has its own hashes, materials, coordinates and bindings.
2. Verify the format, base-level hash and rectangle using the game's own
   texture. Inspect any shared intensity and shadow maps. In a multi-cell
   sheet, use the smallest rectangle that contains the complete original icon.
3. Add the action and rendering layer to the catalog. Compose the replacement
   from the original pixels at runtime. Keep surrounding text, neighboring
   cells, transparency and the distinction between animation states.
4. Verify the original, replacement and restored artwork in the same scene.
   Remap the action and repeat. Exercise device switches while the prompt is
   visible, including a fixed style and automatic mode.

## Verification

On Linux, the first Prince Fluff dialogue showed Space and a remapped J. In
automatic mode it changed from J to A when a virtual Xbox 360 controller was
used and returned to J after keyboard input. A physical DualSense was detected
during the PlayStation-style runs. The introduction showed Options for Skip,
Quilty Square showed Triangle for Area Info and Options for Menu, and the
dialogue and Patch Castle jump sign showed Cross.

A local check using the player's disc verified all 54 catalog textures,
rectangle bounds, preservation of neighboring pixels, remapping and restoration.
Full scene and language coverage, physical Switch validation and Windows
gameplay validation remain open. Wrist-strap and grip illustrations still
depict a Wii Remote.

The NSMBWiicompiled implementation and its icon import scripts were inspected
at revision `0c01832a26aa2559ffd6b6ca489decf22f05c306`. See its
[button glyph implementation](https://github.com/ral1009/NSMBWiicompiled/blob/0c01832a26aa2559ffd6b6ca489decf22f05c306/runtime/src/product/nsmbw_button_glyphs.cpp)
and the [asset credits](../THIRD_PARTY.md).
