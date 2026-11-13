# Additional runtime references

Reviewed on 2026-11-12 while Kirby was stopping at its first BPE worker switch.
These repositories are references, not new build dependencies. Their reported game
milestones have not been independently reproduced here.

| Repository and reviewed revision | Findings | Use in this project |
| --- | --- | --- |
| [WiindowsPlay](https://github.com/SNO2207/WiindowsPlay/tree/5daddaf22f4c2f8ddfd9d87baa5b701f06f612b5) | Its technical guide describes guest scheduler code with Windows fibers/context-transfer hooks, D3D11 rendering and an IOS Bluetooth device. | Architectural comparison only; no code imported. Its [notice](https://github.com/SNO2207/WiindowsPlay/blob/5daddaf22f4c2f8ddfd9d87baa5b701f06f612b5/NOTICE.md) states that no project-wide license is specified. The Windows backend is not a Linux replacement. |
| [NWiiRecomp](https://github.com/BlackLineInteractive/NWiiRecomp/tree/595f176f1d24cc54ff2e8389feed12d7fb553cc2) | Separate analyzer, recompiler and runtime, with its own CPU context, SDL2/OpenGL, HLE services and interpreter fallback described in its README. | Evaluated as an alternative architecture; no code imported or linked. The [license](https://github.com/BlackLineInteractive/NWiiRecomp/blob/595f176f1d24cc54ff2e8389feed12d7fb553cc2/LICENSE) includes noncommercial and distribution restrictions. This project continues with its existing GPL/MIT components. |
| [ResortCompiled](https://github.com/redluigi323/ResortCompiled/tree/604878918c5384cbc0b3928c6bc58877ff3c3128) | WiiCompiled/Aurora runtime with a native fiber manager, Linux libco host contexts and extra rewindable-context support. Its scheduler and worker manager still contain Resort-specific addresses. | Closest integration reference. Reuse the already pinned WiiCompiled `HostContext` backend and adapt Kirby's audited scheduler boundary. No Resort runtime or game-specific patch has been imported. Its repository license is GPL-3.0. |

The relevant Resort sources are
[`host_context.cpp`](https://github.com/redluigi323/ResortCompiled/blob/604878918c5384cbc0b3928c6bc58877ff3c3128/runtime/wsr/src/host_context.cpp),
[`fiber_manager.cpp`](https://github.com/redluigi323/ResortCompiled/blob/604878918c5384cbc0b3928c6bc58877ff3c3128/runtime/wsr/src/fiber_manager.cpp)
and [`os_scheduler.cpp`](https://github.com/redluigi323/ResortCompiled/blob/604878918c5384cbc0b3928c6bc58877ff3c3128/runtime/wsr/src/hle/os/os_scheduler.cpp).
They separate native stack allocation/switching from the guest thread bookkeeping.
That host-context interface is already available in the pinned WiiCompiled runtime:
`InitializeScheduler`, `Create`, `Switch`, `IsCurrent`, `Destroy` and
`ShutdownScheduler`. Its Linux backend uses libco. Kirby still needs the integration
that associates those stacks with its original SDK queues and CPU contexts, including
the active CPU-context pointer and floating-point state during a switch. Running the
standalone host-context test does not prove guest thread scheduling works.

Wario remains the Good-Feel engine reference. None of these projects establishes that
its hard-coded addresses, context-global locations, input layouts or patches apply to
RK5E01. Kirby's DOL and community symbols remain the authority for those details.

Folder names and identifiers describe the project; they do not replace source licenses
or attribution. Existing notices remain intact. Original disc files and generated game
translations stay local and ignored, independently of how authored source is named.

## Performance, audio and launcher review (2026-11-13)

The following are source/reference reviews, not independent executions or benchmarks
of those ports. GitHub stars are a discovery signal, not a quality or speed measure.

| Project and inspected revision | Relevant implementation | Kirby reuse decision |
| --- | --- | --- |
| [ZeldaWWHDRecomp](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/tree/19e1fcc92e5a9a3e2c96f74eb325d8334c42236d) | Wii U, Cafe OS and GX2. Native SDL3/ImGui setup frontend; player-focused README and separate performance/build docs. | Adopt the frontend/toolkit pattern and documentation structure. No code, branding, game files or GX2 implementation copied. It is not a Wii runtime replacement. |
| [Mario Kart WiiCompiled](https://github.com/patchzyy/Wiicompiled/tree/ffbde831317c2a3fc7841f53e7244db83f024ff2) | Native SDK replacements, Aurora GX rendering, audio backend, settings and interpolation. | This is already Kirby's pinned translator/runtime base, not a dependency still waiting to be adopted. Retain original licenses and audit game-specific hooks. |
| [WarioRecomp](https://github.com/santiagovalencia109/Wario-Land-Shake-It-Native-PC/tree/5974bf059d58f946c704c7589b27cb44ef0c10f1) | Good-Feel reference, Linux build, AX/AI HLE and Avalonia launcher. README reports audio, levels and saves working, with Linux still experimental. | Closest engine comparison. Review its audio/graphics boundaries; translate no Wario addresses into Kirby without DOL evidence. |
| [New Super Paper Bros.](https://github.com/josiahg2502-glitch/NewSuperPaperBros/tree/1f961e7db18586655f647f1027eb86ca706088cc) | WiiCompiled-based Wii port, native input/audio, module integration and a Linux launcher. | Useful setup and compatibility reference; its title-specific module layout and patches do not apply automatically. |
| [Brawl Recompiled](https://github.com/josiahg2502-glitch/BrawlRecompiled/tree/a77be8847ec9d8cf6128f086fe9c493d105ecbce) | WiiCompiled-based Wii port with many RELs, an in-game options UI and interpolation. | Reference for runtime/UI boundaries. Additional Brawl module and mod support is outside Kirby's initial playable scope. |

At review, the GitHub API reported 1,470 stars for ZeldaWWHDRecomp (Wii U), 1,379
for WiiCompiled, 196 for New Super Paper Bros. and 99 for WarioRecomp. This is a
bounded comparison of relevant public projects, not an exhaustive popularity ranking.

The Zelda frontend's [installer documentation](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/blob/19e1fcc92e5a9a3e2c96f74eb325d8334c42236d/tools/installer/README.md)
separates a native SDL3/ImGui interface from local extraction/build tools. Kirby's
first frontend reuses its already-built SDL3/ImGui libraries and invokes the local
native game; full first-run installation is not implemented. The Zelda source is
MPL-2.0; it was reviewed without importing code into the GPL runtime.

The [performance notes](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/blob/19e1fcc92e5a9a3e2c96f74eb325d8334c42236d/docs/performance.md)
compare warmed identical scenes and distinguish renderer frames from logic steps.
Kirby's current samples do not meet that A/B standard. See [local findings](performance.md).

Kirby's current integration executes more of the original SDK and models hardware
boundaries, including synchronous RAM-visible GPU copies. Recompiling the PPC
instructions does not remove those synchronization costs. The existing boot work
made gameplay observable, but synthetic correctness checks did not establish frame
pacing or audible sound. Performance and audio now need measured end-to-end checks;
adding more neutral controller captures would not address either report.
