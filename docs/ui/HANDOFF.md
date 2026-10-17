# ROLLA — concept_FINAL · JUCE handoff

Base: **D simple (dark)**. Single change in intent: the ROLL tile becomes the **face-ROLL** (A's evil-grin smiley, redrawn on a 4px grid). Editor is fixed-aspect **1024 × 640 @1x (16:10)**, all vector, scales 100 / 150 / 200 %.

Files: `index.html` (self-contained, fonts embedded) · `screenshot@2x.png` (BASS selected) · `screenshot_drums@2x.png` (DRUMS selected) · `face.svg` · `face_states@2x.png`.
Preview states in the browser: `index.html#bass-hover`, `#bass-press`, `#bass-rolling` (frozen mid-wink), `#drums`, `#vox`, … Click the face or press **Space** to roll.

---

## 1. Component tree

```
RollaEditor (AudioProcessorEditor, 1024×640 logical, fixed aspect)
├── Header
│   ├── Logo ("ROLLA" wordmark, text)
│   ├── DialectSelector          segmented: UK MIN · LONG · POP · GASKIN 90s
│   ├── KeyStepper               ◀  D MIN  ▶
│   ├── BpmReadout               host tempo, read-only
│   ├── UndoButton / RedoButton
├── LaneList
│   └── LaneRow ×7               BASS STAB HITS PLUCK PAD VOX DRUMS
│       ├── LaneDot + LaneName   (click row = select lane)
│       ├── PatternStrip         read-only preview of the current pattern
│       ├── MuteButton (M)
│       ├── ShuffleButton (dice) re-rolls this lane only
│       └── KeepButton (lock)    + EditedMarker (amber dot) when auto-kept by a manual edit
├── SoundPanel                   shows the selected lane only
│   ├── PanelHeader              dot · name · "ch n · sound" (drums: kit stepper + edited note)
│   ├── LaneSound   (6 melodic lanes)
│   │   ├── PresetStepper
│   │   ├── [VOX] SampleDropSlot (overrides preset, eject ×)
│   │   ├── [HITS] InStabToggle
│   │   └── Knob64 ×3            TONE (bipolar) · PUNCH · LEVEL (dB)
│   └── DrumVoices  (DRUMS lane)
│       └── DrumVoiceColumn ×7   KICK SNARE CLAP RIM CH OH RIDE
│           ├── SampleDropSlot (compact)
│           ├── Knob32 TUNE (bipolar) · Knob32 DECAY
│           ├── LevelBar (horizontal fill slider)
│           └── AccentToggle     (right-click → accent amount popover)
├── FaceRollButton               hero: face + "Roll" + SPACE keycap + "n lanes · X kept"
├── MacroBank                    Knob64 ×4  DARK · BUSY · BOUNCE · STAB (+ live descriptor)
├── SecondaryRow                 SWING slider (50–57 %, value) · DUCK toggle
└── DragMidiTile                 drag-out .mid (all lanes) + EXPORT button
```

## 2. Tokens

### Colour (dark)
| token | hex | use |
|---|---|---|
| paper | `#111214` | editor background, input wells, face ink |
| card | `#1A1B1E` | panels / tiles |
| hov | `#222427` | hover fill (rows, buttons, roll tile) |
| sel | `#26282C` | selected lane row |
| line | `#2A2C30` | 1px panel outlines, row separators |
| line2 | `#3A3D42` | control outlines, inactive slider track |
| ink | `#ECEAE4` | primary text, value arcs, ON fills (lock, accent, dialect) |
| ink2 | `#ADAAA3` | secondary text, icons, captions |
| ink3 | `#8E8B84` | tertiary text, dashed drop-zone outline |
| ktrack | `#33363B` | knob track |
| kcap | `#2C2F34` | knob cap (hover `#36393F`) |
| mark | `#E9B32A` | edited / auto-kept marker only |
| mute | `#ADAAA3` | mute ON fill (text `#111214`) |
| **acc** | **`#FF6A3D`** | **face head — the only hero accent, used nowhere else** |
| acc2 | `#FF7D55` | face head on hover |
| faceink | `#111214` | face features |
| lane: bass / stab / hits / pluck / pad / vox / drums | `#3CC49B` / `#B9D66B` / `#E9B32A` / `#6EA2E3` / `#93A9B9` / `#E46BA4` / `#ECEAE4` | lane dot, selected bar, pattern strip |

Contrast (WCAG): ink/card 14.3 · ink2/card 7.4 · ink3/card 5.1 · faceink/acc 6.6 · acc/card 6.1 · dark text on ink 15.6 · dark text on mute 8.1.

### Type (2 families, embedded via BinaryData)
| role | family | size / weight | tracking |
|---|---|---|---|
| Roll title | Instrument Sans | 40 / 700 | −1 % |
| Logo | Instrument Sans | 18 / 700 | −1 % |
| Panel title | Instrument Sans | 16 / 700 | +2 % |
| Preset name | Instrument Sans | 13 / 700 | +2 % |
| Lane name, macro name, seg | Instrument Sans | 12 / 600–700 | +6–10 % caps |
| Voice name | Instrument Sans | 11 / 700 | +6 % caps |
| Caption / button | Instrument Sans | 10 / 600–700 | +6–10 % caps |
| Macro value | Geist Mono | 14 / 600 | 0 |
| Knob value, meta, roll label | Geist Mono | 11–12 / 400–500 | 0 |
| Sub-values, slot text, kbd | Geist Mono | 10 / 400 | 0 |

### Spacing, radii, strokes
- Grid 8 px; outer margin 16; panel gap 16 (column) / 8 (stacked right column); panel inner padding 16.
- Control heights: 40 (header controls, preset stepper), 32 (row icons, kit stepper, export, drop slot), 24 (LevelBar, Accent), 28×44 (switch).
- Knob sizes: **64** (TONE/PUNCH/LEVEL, macros) and **32** (drum TUNE/DECAY) — only two.
- Radii: panels 8 · drop slots 6 · kbd 4 · pills = h/2 (40→20, 32→16, 24→12).
- Strokes: outlines 1 px (drawn *inside* the bounds); knob arc 4 px @64 / 3 px @32, butt caps; pointer 2.5 / 2 px round; face features 6 px round.
- Knob geometry: sweep −135°…+135°; arc radius = r − stroke/2; cap radius 0.64·r (64) / 0.60·r (32); pointer from 0.20 to 0.85 of cap radius. Bipolar knobs fill from 12 o'clock.

## 3. Layout @1x (x, y, w, h)

All outlines are inset strokes, so these are the real bounds. Multiply by the scale factor.

| component | x | y | w | h |
|---|---|---|---|---|
| Header (bottom 1px line) | 16 | 0 | 992 | 56 |
| Logo | 16 | 16 | 60 | 24 |
| DialectSelector (buttons inset 4, h32) | 100 | 8 | 312 | 40 |
| KeyStepper | 428 | 8 | 136 | 40 |
| BpmReadout | 832 | 20 | 64 | 16 |
| Undo / Redo | 912 / 968 | 8 | 40 | 40 |
| LaneList | 16 | 64 | 640 | 280 |
| LaneRow[i] | 16 | 64 + 40·i | 640 | 40 |
| · dot+name | 32 | row+12 | 80 | 16 |
| · PatternStrip | 120 | row+8 | 408 | 24 |
| · Mute / Shuffle / Keep | 536 / 576 / 616 | row+4 | 32 | 32 |
| SoundPanel | 16 | 352 | 640 | 272 |
| · PanelHeader | 16 | 352 | 640 | 48 |
| · PresetStepper (caption at y440) | 32 | 464 | 256 | 40 |
| · VOX SampleDropSlot (caption at y520) | 32 | 544 | 240 | 56 |
| · TONE / PUNCH / LEVEL knob | 328 / 424 / 520 | 464 | 64 | 64 |
| · DRUMS kit stepper | 264 | 360 | 176 | 32 |
| · DrumVoiceColumn[i] | 32 + 88·i | 416 | 80 | 188 |
| ·· name row | col | 416 | 80 | 16 |
| ·· SampleDropSlot | col | 440 | 80 | 32 |
| ·· TUNE / DECAY knob | col+4 / col+44 | 496 | 32 | 32 |
| ·· LevelBar | col | 548 | 80 | 24 |
| ·· AccentToggle | col | 580 | 80 | 24 |
| FaceRollButton | 672 | 64 | 336 | 128 |
| · Face | 688 | 80 | 96 | 96 |
| · "Roll" | 800 | 96 | 72 | 40 |
| · SPACE kbd | 944 | 104 | 48 | 20 |
| · lanes label | 800 | 144 | 192 | 16 |
| MacroBank | 672 | 200 | 336 | 272 |
| · DARK / BUSY knob | 724 / 892 | 228 | 64 | 64 |
| · BOUNCE / STAB knob | 724 / 892 | 364 | 64 | 64 |
| SecondaryRow | 672 | 480 | 336 | 48 |
| · SWING slider (hit 80×24) | 744 | 492 | 80 | 24 |
| · DUCK switch | 956 | 490 | 44 | 28 |
| DragMidiTile (1px dashed) | 672 | 536 | 336 | 88 |
| · EXPORT | 920 | 564 | 72 | 32 |

Hit targets: everything ≥ 32 px except LevelBar / Accent (24 h × 80 w) and the SWING thumb (24 px hit box).

## 4. States

| control | idle | hover | pressed | active / on | disabled | selected | edited-kept | muted |
|---|---|---|---|---|---|---|---|---|
| LaneRow | card bg, name ink2 | bg hov, name ink | — | — | — | bg sel, 4px lane-colour bar at left, 1px line2 outline, name ink | — | name + strip 40 % opacity |
| Mute (M) | transparent, ink2 | bg line, ink | bg line2, +1px y | fill mute `#ADAAA3`, text `#111214` | — | — | — | = on |
| Shuffle (dice) | ink2 icon | bg line | bg line2, +1px y | — (momentary) | 35 % when lane is kept | — | — | — |
| Keep (lock) | open lock ink2 | bg line | bg line2 | ink fill, closed lock paper | — | — | ON + 8px mark dot top-right (2px sel ring) | — |
| Dialect seg | ink2 text | bg hov | bg line | ink fill, paper text | — | — | — | — |
| Steppers (key, preset, kit) | outline line2 | arrow bg hov | arrow bg line | — | arrow 35 % at list end | — | preset struck through + ink3 when VOX sample overrides | — |
| Knob64 / Knob32 | track ktrack, fill ink, cap kcap | cap `#36393F` | drag: no visual change; value text ink | — | 35 % | — | — | — |
| LevelBar | paper well, line2 fill | outline ink2 | — | — | 35 % | — | — | — |
| AccentToggle | paper, line2 outline, ink2 text | ink text, ink2 outline | — | ink fill, paper text | — | — | — | — |
| SampleDropSlot | 1px dashed ink3, "drop" | outline ink | — | file loaded: solid ink2 outline, paper well, eject × | — | drag-over: solid ink outline | — | — |
| DUCK switch | track line2, knob ink2 | — | — | track ink, knob paper, right | — | — | — | — |
| Undo / Redo | outline line2, ink2 | bg hov | bg line | — | 35 % (redo with an empty stack) | — | — | — |
| DragMidiTile | dashed ink3 | dashed ink | cursor grabbing; tile follows the OS drag | — | — | — | — | — |
| FaceRollButton | see §5 | see §5 | see §5 | rolling, see §5 | 35 % when every lane is kept (label "all kept") | — | label omits kept lanes | label omits muted lanes |

Focus (keyboard): 2px ink outline, 2px offset, on every focusable control.

## 5. Face-ROLL

96×96 viewBox, every coordinate on a 2px grid and the key points on 4px. Head: a filled circle r48. Features: 6px stroke with round caps. Eyes: filled r6 (diameter = 2 × stroke), so brows, mouth and eyes share one visual weight. There is no gloss, gradient, shadow or outline.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 96 96" width="96" height="96">
<title>ROLLA face-ROLL mark</title>
<desc>96x96 grid (4px-friendly). Head = filled circle r48. Features = stroke 6, round caps, eyes = filled r6 (diameter = 2x stroke). Only one state group is visible at a time; "pressed" is a transform on #face (scale 1.04 x 0.96, origin bottom centre).</desc>
<g id="face">
  <circle id="head" cx="48" cy="48" r="48" fill="#FF6A3D"/>
  <g id="brows" fill="none" stroke="#111214" stroke-width="6" stroke-linecap="round">
    <g id="brows-idle"><path id="brow-l" d="M24 24 L42 32"/><path id="brow-r" d="M72 24 L54 32"/></g>
    <g id="brows-rolling" display="none"><path d="M24 20 L42 30"/><path d="M72 22 L54 31"/></g>
  </g>
  <g id="eyes" fill="#111214">
    <g id="eyes-idle"><circle id="eye-l" cx="36" cy="44" r="6"/><circle id="eye-r" cx="60" cy="44" r="6"/></g>
    <g id="eyes-hover" display="none"><rect x="30" y="41" width="12" height="6" rx="3"/><rect x="54" y="41" width="12" height="6" rx="3"/></g>
    <g id="eyes-wink" display="none"><path d="M30 45 Q36 40 42 45" fill="none" stroke="#111214" stroke-width="6" stroke-linecap="round"/><circle cx="60" cy="44" r="6"/></g>
  </g>
  <g id="mouth" fill="none" stroke="#111214" stroke-width="6" stroke-linecap="round">
    <path id="mouth-idle" d="M24 60 C32 72 64 72 72 60"/>
    <path id="mouth-rolling" display="none" d="M22 58 C30 74 66 74 74 58"/>
  </g>
</g>
</svg>
```

| state | trigger | change | timing |
|---|---|---|---|
| idle | — | as above | — |
| hover | mouse over tile | eyes scaleY 0.5 around their own centre (squint); head → acc2 `#FF7D55`; tile bg hov, outline line2 | 120 ms, cubic-bezier(.3,.7,.4,1) |
| pressed | mouse down | whole face scale(1.04, 0.96) around the bottom centre (48,96); eyes stay squinted; tile bg `#1E2023` | 90 ms, same curve; release springs back over 90 ms |
| rolling | click-up, Space, host-mapped ROLL | 340 ms one-shot, cubic-bezier(.2,.7,.2,1): **left eye** closes (scaleY 1→0.15 over 0–18 %), swaps to the wink arc 22–62 %, reopens 66–100 % · **brows** lift −3 px (peak at 30 %, back by 70 %) and the **right brow** also rotates −8° around its own centre · **mouth** scales 1.10 × 1.15 from its centre (peak at 35 %, back by 75 %) | 340 ms; a re-trigger restarts it |
| reduced motion | OS setting | no tweens; rolling = instant swap to the wink + rolling brows/mouth for 300 ms, then back | — |

JUCE drawing (FaceRollButton::paintButton): build `Path`s once in `resized()` from the coordinates above, scaled by `getWidth()/96`. Per frame, take `t` (0–1) and apply an `AffineTransform` per feature: eyes `scale(1, sy, cx, cy)`; brows `translation(0, dy)` plus `rotation(θ, 63, 28)` for the right brow; mouth `scale(1.1k, 1.15k, 48, 66)`. The wink is a separate Path drawn with alpha. Strokes use `PathStrokeType(6·s, curved, rounded)`. The keyframes reduce to piecewise-linear segments on an eased `t`, which is easy to table.

## 6. LookAndFeel_V4 mapping (`RollaLookAndFeel`)

- **ColourScheme / setColour:** map the tokens above with `ResizableWindow::backgroundColourId = paper`, `Slider::rotarySliderFillColourId = ink`, `rotarySliderOutlineColourId = ktrack`, `thumbColourId = kptr`, `TextButton::buttonColourId = card`, `buttonOnColourId = ink`, `textColourOffId = ink2`, `textColourOnId = paper`, `ComboBox::*`, `PopupMenu::backgroundColourId = card`, `highlightedBackgroundColourId = hov`.
- **drawRotarySlider:** track arc (−135°…+135°, butt caps, width = round(size/16)), value arc (bipolar if `slider.getProperties()["bipolar"]`), cap circle (0.64/0.60 r, kcap; hover `#36393F`), pointer line (round caps). Values are drawn by a separate `Label`, not by the knob.
- **drawButtonBackground:** pill (h/2 radius) or 8 px panel depending on the `"shape"` property; fill card / hov / line for idle / hover / down; 1 px inset outline line2. The ON state fills ink. The face tile background is handled here too.
- **drawToggleButton:** routes on a `"kind"` property: `mute` (round 32, mute fill), `lock` (lock glyph path, ink fill, plus the edited dot from a `"edited"` property), `accent` (pill 24), `switch` (44×28 track + 20 px knob).
- **drawLinearSlider:** SWING (4 px track line2, ink fill from min, 4×16 ink thumb with a 24 px hit area) and LevelBar (`LinearBar` style: paper well, line2 fill, value text right-aligned in Geist Mono 10).
- **drawComboBox / drawPopupMenuItem / drawPopupMenuBackground:** dialect and key overflow menus plus the accent amount popover: card bg, 1 px line2, 8 px radius, 32 px items, hov highlight, a ✓ drawn as a Path.
- **getLabelFont / getTextButtonFont / getPopupMenuFont:** return the two embedded typefaces at the sizes in §2.

## 7. Custom components

- **FaceRollButton : Button.** Holds the per-state paths (idle/hover eyes, wink, rolling brows/mouth) and a `float rollT`. Animate with `juce::VBlankAttachment` (JUCE 7.0.6+): advance `rollT` from real elapsed ms, `repaint()` only the face bounds, and detach when done. On older JUCE, use a `Timer` at 60 Hz. Hover and press tweens run on the same clock (120 / 90 ms). `keyPressed(Space)` calls `triggerClick()` on the editor; check whether the host swallows Space (FL usually does), see Q3. Respect reduced motion via a user setting, since there is no OS query in JUCE (see Q4). The label text is built from the processor's lane state: "n lanes · A + B kept".
- **PatternStrip : Component.** Paints from a snapshot of the lane's step data (16-step × 4-bar grid, note lengths as rect widths, lane colour). Drums use 3 rows (kick/hat/snare+clap) in ink / ink2 / ink. Repaint on pattern-change messages only; never on the audio thread.
- **LaneRow : Component.** Selection on mouseDown, plus the 3 icon buttons. Owns the selected/hover/muted visuals and the 4 px lane bar. Icons are `Path`s from SVG (`Drawable::createFromSVG` once, or hand-coded).
- **SampleDropSlot : Component, FileDragAndDropTarget.** `isInterestedInFileDrag` accepts wav/aif/aiff/flac; `fileDragEnter`/`Exit` draw the drag-over state. A loaded slot shows the name in Geist Mono plus an eject button. A VOX sample overrides the preset, which shows struck through.
- **DragMidiTile : Component.** In `mouseDrag` (after 4 px), write the current pattern to a temp `.mid` (`MidiFile`, 96 ppq, one track per unmuted lane) and call `DragAndDropContainer::performExternalDragDropOfFiles({file}, false)`. FL Studio accepts file drops from plugins. EXPORT opens a `FileChooser`.
- **Scaling:** lay out everything in logical 1024×640 inside a `content` component, then call `content.setTransform(AffineTransform::scale(s))` with `s = getWidth()/1024`. Constrain with `setResizable(true, true)` + `getConstrainer()->setFixedAspectRatio(1024.0/640.0)` and `setResizeLimits(1024,640,2048,1280)`. Offer a 100/150/200 % menu via `setSize`. Alternatively, use `setScaleFactor` for host-driven scaling. All drawing is Path/Graphics vector work, so no bitmaps are needed.
- **Fonts:** InstrumentSans (variable, wdth/wght) and GeistMono (variable) both go in `BinaryData`. Load them with `Typeface::createSystemTypefaceFor(BinaryData::..., size)`. JUCE doesn't drive variable axes, so export static instances: InstrumentSans 600 and 700 (with 400 optional), and GeistMono 400/500/600. Both fonts are SIL OFL.

## Open questions
1. Should brow angle track DARK (e.g. 0° at DARK 0 → current 24° at 10)? Cheap to do, and it makes the face a live readout. My view: yes, but subtle (±6°).
2. The accent is face-only. Should the ROLL keycap or the focus ring pick it up? I'd keep it exclusive.
3. Space usually belongs to FL's transport when the plugin window has focus. Keep Space for ROLL only when the editor has keyboard focus, and also expose ROLL as an automatable/MIDI-learnable parameter?
4. Reduced motion: JUCE can't read the OS setting. Add a "Reduce motion" item in the settings menu?
5. Disabled ROLL when every lane is kept: should it go fully disabled (35 %), or stay clickable and nudge with a brow-only twitch?
