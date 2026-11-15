# Gameplay validation

These checks describe observed native gameplay, not whole-game compatibility or
display performance. Game data, test saves and framebuffer captures stay local.

## Fountain Gardens

The Linux runtime at `297e01ca6f7d00923ac3b0fc82d6a0c6d03d1c6a` was tested on
2026-11-15 on an Acer Nitro AN515-55 (i5-10300H, RTX 3050), using an isolated
copy of a save with Patch Castle already cleared.
An ordinary SDL gamepad supplied movement, jump, yarn and menu inputs. Player
state was read for observation; coordinates and progression flags were not edited.

| Check | Observed result |
| --- | --- |
| Start from the file menu and enter Grass Land | Passed |
| Select and enter Fountain Gardens | Passed |
| Move in both directions and jump | Passed |
| Enter the pool and become a submarine | Passed |
| Swim horizontally and upward | Passed |
| Pull the pool button, fold the fabric and jump across | Passed |
| Use yarn against enemies and break the bead blocks | Passed |
| Use the weight action and traverse the fountain area | Passed |
| Open the music treasure and dismiss its reward screen | Passed |
| Reveal the castle entrance, climb inside and exit the right tower | Passed |
| Activate Tankbot, move, fire, jump and destroy the first barrier | Passed |
| Traverse the Tankbot section and return to normal Kirby | Passed |
| Ring the Bonus Bell and reach the level-clear results | Passed |
| Cold restart and reload the clear with its Butterfly patch | Passed |

The clear run recorded **125 beads and a 49-bead streak**. It did not collect
treasures. The music-treasure check was a separate run of the same binary; this
is not a 100% collectible or whole-game completion result. The folded-pool
crossing was repeated in independent game sessions.

The clear screen awarded the Butterfly patch. After confirming the reward, the
game returned to Grass Land and updated the isolated save. The original player's
settings and save were unchanged. A separate process loaded a copy of that save
and resumed Grass Land with the awarded patch and Fountain Gardens clear medal.

The pool crossing requires landing on the raised left rim after folding the
fabric, then jumping across. Earlier attempts fell into the pool; that did not
establish a runtime failure. The music treasure is on the upper cloud route.
Return to the ground to approach the castle entrance.

The test used Vulkan, an unmapped window, experimental GPU-resident copies,
VSync disabled, a dummy audio device and runtime volume zero. It does not verify
visible frame pacing, audible sound, physical controllers or Legion Go hardware.
Framebuffer inspection interrupts execution and is excluded from performance
measurements. See [performance measurements](performance.md) for those separate
checks and their limitations.

## Flower Fields: partial traversal

A separate native run on the same Acer used clean runtime `699f568` and an
isolated copy of the Fountain Gardens clear save after placing the Butterfly
patch. Grass Land displayed the Flower Fields door and selection screen. The
loaded stage was `(2, 102, 1)`, matching the stage/resource IDs in Swiftshine's
`src/fluff/stage/StageInfo.h`.

Observed controls included movement in both directions, jumps, yarn attacks,
an upward yarn grab on a flower, swinging and jumping from that grab to cross
the first high wall. The subsequent pool activated Submarine transformation
`1`, matching `PlayerBase.h`; horizontal and upward movement were observed.
The run reached x=198.575 in section 1. Coordinates and progression flags were
only read, never edited.

The pool exit, later transformations and level clear remain unverified. Stopping
at the pool does not establish a runtime or collision failure; the traversal
route still needs work. The test process was closed, and the original player's
save and settings retained their prior hashes. This was another hidden Vulkan
functional test with GPU-resident copies, VSync off, dummy audio and volume zero.
Debugger-assisted framebuffer captures were inspected locally and are excluded
from performance claims. Evidence remains under ignored `build-logs/stage103-*`.
