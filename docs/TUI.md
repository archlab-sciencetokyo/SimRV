# Terminal UI Architecture

The TUI is organized into a modular internal framework:

1. **Framework Core (`include/simrv/tui/framework/`)**:
   - `Layout.hpp` & `Terminal.hpp`: Cell-based geometry calculation, box drawing, multi-column sizing, clip regions, and split/full layout policies.
   - `Theme.hpp` & `Text.hpp`: Semantic ANSI color palette tokens, word-wrapping, padding, column cropping, and cell width measurement.
   - `Components.hpp` & `Modal.hpp`: Reusable modal frames, radio button selectors, sliders, action buttons, and control hitboxes.
   - `ScrollView.hpp`: Reusable 2D viewport slicing, coordinate conversion, horizontal/vertical bounding, and scroll indicators.
2. **Input Routing & Terminal (`TuiInputRouter.hpp`, `VirtualTerminal.hpp`)**:
   - `TuiInputRouter.hpp` classifies each input byte and key action using modal, pause, and terminal-focus state.
   - `VirtualTerminal.hpp` parses guest ANSI/UTF-8 terminal sequences into a bounded screen buffer and scrollback history.
3. **Domain Modals (`include/simrv/tui/modals/`)**:
   - `SettingsModal`, `SystemConfigModal`, `GlossaryModal`, `LoadModal`, `MisaModal`, `BreakpointModal`, `AddressModal`, `StepModal`.
4. **Panels & Telemetry (`include/simrv/tui/panels/`)**:
   - `InspectorPane`: Multi-view tool inspector (Registers, Stack Watch, Pipeline, Cache, TLB, Breakpoints, Hazards, I/O, Performance Stats, Instruction Trace, Microarchitecture Explainer).
   - `TerminalPane`: Guest virtual terminal console, disassembly inspector, and Sixel display.
   - `StatusBar` and `Header`: Contextual hotkey legend, telemetry metrics, and hart selectors.

## Input and Focus

The dedicated UI thread exclusively owns stdin polling, escape-sequence parsing, terminal-size
updates, and drawing. Simulation threads enqueue guest UART bytes and request a render; they never
read stdin or render directly. This prevents concurrent consumers from dropping guest keystrokes or
splitting ANSI control sequences.

When the guest terminal is focused and running, ordinary bytes—including carriage return and
newline—are delivered directly to the platform UART (`ttyS0`). `Ctrl-P` pauses and `Ctrl-Q` quits.
`[Ctrl-A]` (`KeyAction::ToggleTerminalFocus`) toggles keyboard input focus between the guest terminal PTY/UART
and TUI navigation controls. While paused, keys control TUI navigation. An active modal
receives input before navigation, except global `Ctrl-R` reboot and `Ctrl-Q` quit. These controls
remain available after guest shutdown and wake the stopped simulation loop for clean teardown or
restart. The optional VirtIO console is a separate device and does not
receive copies of UART keystrokes.

The host terminal's carriage-return Enter byte is normalized to newline at the virtual-terminal
boundary, matching PTY line input and shells that read the UART without enabling `ICRNL` themselves.

Press `g` to open the interactive **Educational Glossary** modal containing comprehensive RISC-V and computer architecture concept definitions.

Frame and modal geometry share one resize policy across split and full-pane layouts. Modal borders
remain closed at the 40×10 minimum; when content cannot fit, the last visible row reports that the
terminal should be resized instead of silently rendering an open or torn overlay.

Clickable footer labels and the action portion of online help are generated from the canonical
keybinding registry. Rendering and hit-testing use the same captured terminal dimensions, avoiding
stale labels or mouse targets after a resize.

Full-screen border composition is a pure render step shared by the live TUI and native tests.
Representative 40×10, 80×24, 120×32, and 160×48 split/full layouts assert exact row width, row
count, outer borders, and center-divider presence.

Escape starts ANSI/control-sequence parsing in every state. A standalone Escape closes a modal when
that modal permits cancellation.

## Design contract

The TUI communicates interaction before decoration: filled controls are actionable; plain text is
state. Themes map semantic roles rather than hard-coded colours:

| Role | Use |
| --- | --- |
| `surface` / `border` | Modal backgrounds and structural frames |
| `text` / `muted` | Primary and secondary passive copy |
| `value` | Machine state and inspected data |
| `accent` | Focus, navigation, and actionable controls |
| `success` / `warning` / `danger` | Active, paused/changed, and failure/trap states |

Renderers measure terminal cells, not UTF-8 bytes; ANSI sequences have zero width. A renderer
returns its requested width unless it is explicitly a fragment builder. Drawing and hit-testing
share `RenderedControl` geometry. Narrow layouts shorten labels before removing important state,
and the Classic ANSI theme remains usable without Unicode glyphs.

Selected keycaps, badges, and primary tabs are filled and clickable. Passive telemetry remains
plain text. Modal titles and action rows are centred; forms and explanatory content are
left-aligned. Scrollable regions show direction markers whenever content exists beyond the view.

## Tests

`tui-framework` is a native CTest covering input routing, CR/LF, cursor movement, SGR attributes,
cursor visibility, scrollback, selection, resize/reset behavior, UTF-8 cells, themes, and keybinding
registry integrity. It is part of the `gate`, `regress`, and `tui` labels and runs under sanitizer
configurations.

While simulation is running, status and CA performance views consume stable per-hart snapshots.
Detailed register, pipeline, cache, and TLB inspection resumes only after all SMP workers have
acknowledged pause.

`linux-boot-pty` is the baseline end-to-end test for Linux boot and TUI/PTY/UART interaction. The
`linux-ca-boot-pty`, `linux-ca-quantum-smp-pty`, and `linux-ca-mt-smp-pty` variants cover CA and
verify that both CPUs reach the shell in SMP configurations. The tests run the TUI
under a pseudo-terminal, wait for getty, send Enter, and verify a command executed by the guest
shell. Run the focused tests with:

```bash
ctest --test-dir build/rv64-release --output-on-failure -R 'tui-framework|linux-(boot|ca-.*)-pty'
```
