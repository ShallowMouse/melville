# MELVILLE

Hawthorne, EMERSON, WHITMAN, THOREAU and PEQUOD Host as **one program**, from PEQUOD.

MELVILLE owns the audio interface. Every part runs inside it, off the interface's clock, with a patchbay in the middle. Each part keeps its own window and its own look. The parts are the same code as the separate apps, and the separate apps are still built and released on their own.

```
                       ┌───────────── MELVILLE patchbay ─────────────┐
 interface in ──▶ PEQUOD Host ──▶ THOREAU ──▶ Hawthorne ──▶ EMERSON ──▶ WHITMAN
                                                ▲             │  ▲         │
                                                └─────────────┘  └─────────┘
                                         (Route, inside MELVILLE)  (EMERSON Link, inside MELVILLE)
 interface out ◀── whatever you patch there (by default, Hawthorne's outputs)
```

## Using it

The **MELVILLE bar** opens with Hawthorne. It has a button for each part and a lamp under each one that lights while the part is running. Click a button to bring up that part's window. Closing a part's window only hides it: the tape keeps rolling, the rack and the desk keep processing. Closing the bar quits MELVILLE.

* **INTERFACE** picks the audio interface, sample rate and buffer. That's the only place you pick a device now. Each part's own SETTINGS shows its MELVILLE device (for example *Hawthorne Route (MELVILLE)* in EMERSON), already set up.
* **MELVILLE LINK** (was Hawthorne Link), moved from Hawthorne into MELVILLE: connect senders on other PCs (THOREAU, PEQUOD Host, anything with the Hawthorne Link sender). Each connected sender gets a block of jacks on the **MELVILLE LINK** patch panel, to cable wherever you like (into Hawthorne's MELVILLE buses to record it, say). Senders still follow Hawthorne's tape position and tempo. The first time, MELVILLE picks up the senders Hawthorne was connected to.
* **PATCHBAY** shows a grid. Pick what you're patching **FROM** (a part's outputs) and **TO** (a part's inputs), then click a square to make or break a cord. One source can feed any number of inputs, and an input with several cords hears them summed. Right-click a cord to set its level (0 to -18 dB). **DEFAULT** puts the starting patch back. **SAVE** and **OPEN** keep patches as `.melpatch` files. The patch is also kept between runs.

### The default patch

| From | To |
|---|---|
| Hawthorne out 1–64 | Interface out 1–64 |
| Hawthorne's tracks, master and LTC | EMERSON, as Hawthorne Route did |
| EMERSON's TRACKS OUT and returns | WHITMAN, as the EMERSON Link did |
| WHITMAN's mix and sends | back to EMERSON, then on into Hawthorne's master inputs |
| Interface in 1–24 | Hawthorne TRACK IN 1–24 |
| Interface in 1 | PEQUOD Host in 1 (the guitar) |

Each Hawthorne track records whatever is cabled to its **TRACK IN** jack; to record THOREAU or the guitar rig, cable it to the tracks you want. The 2-track records **MASTER IN**, and LTC chase reads **LTC IN**.

EMERSON has one set of track outputs (**TRACKS OUT**). For a hardware desk, cable them to the interface's outputs as well as, or instead of, WHITMAN: inside MELVILLE EMERSON has no TO WHITMAN / DIRECT OUTS switch.

### Timing

Everything runs in one audio callback at the interface's rate and buffer size. The parts run in this order: PEQUOD Host, THOREAU, Hawthorne, EMERSON, WHITMAN. A part hears what runs before it in the same block, with no added delay. Hawthorne → EMERSON → WHITMAN is therefore sample-locked with no extra buffering, which removes the one-buffer lag that WHITMAN's mix had over the EMERSON Link. Anything going back *into* Hawthorne, such as the mix to its 2-track, arrives through Hawthorne Route's input, which Hawthorne already delays and reports as before.

THOREAU still follows Hawthorne's tempo and position over Hawthorne Sync, as it does now.

## How it's built

* `Source/Core`: the hub (owns the interface and runs the parts), the patchbay, the **MELVILLE** device type each part opens instead of an interface, and MELVILLE's end of Hawthorne Route and the EMERSON Link (`Bridges`).
* `Source/Parts`: makes each app's main screen with that app's own settings.
* `Source/Hosted/MelvilleHosted.h`: what the apps call. Each app has a small `Source/Melville.h`: when the app is built into MELVILLE, it opens its MELVILLE device, leaves its saved device settings alone and turns off its own updater. Built on its own, the hooks do nothing.
* Inside MELVILLE, Hawthorne Route and the EMERSON Link use MELVILLE's own shared memory (names prefixed `Melville.`). A standalone Hawthorne or EMERSON running at the same time keeps to its own memory.
* `cmake/Parts.cmake` compiles every app's sources into the one program. A few names the apps each define their own copy of are renamed per app, so the copies don't collide: the PEQUOD look's `Pequod` namespace, `PowerButton`, `PeakMeter`, `LearnableSlider`, `LearnableButton`, `Updater`, and THOREAU's and PEQUOD Host's copies of the Hawthorne Link sender.

## Building

The Windows build (`.github/workflows/build-windows.yml`) checks out every app into `parts/`, builds the PEQUOD engine (Rust) and MELVILLE, runs the core tests and a smoke test that every part starts, then makes the installer.

Locally:

```
git clone https://github.com/ShallowMouse/hawthorne   parts/hawthorne
git clone https://github.com/ShallowMouse/emerson     parts/emerson
git clone https://github.com/ShallowMouse/whitman     parts/whitman
git clone https://github.com/ShallowMouse/thoreau     parts/thoreau
git clone https://github.com/ShallowMouse/PEQUOD-HOST parts/pequod-host
cargo build --release -p pequod_engine --manifest-path parts/pequod-host/engine/Cargo.toml --target x86_64-pc-windows-msvc
cmake -S . -B build -A x64 -DMELVILLE_BUILD_TESTS=ON
cmake --build build --config Release
```

## Not in yet

* One file for the whole studio (reel, rack, desk, kit, rig and patch together). For now each part saves its own as before, and MELVILLE saves the patch.
* A MELVILLE transport and tempo for everything, plus MIDI clock / MTC out to hardware. THOREAU follows Hawthorne Sync for now.
* MELVILLE updating itself (releases go to ShallowMouse/Melville-Releases).
