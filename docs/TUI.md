# Terminal UI architecture

The TUI is divided into four layers:

1. `TuiInputRouter.hpp` classifies each input byte using only modal, pause, and terminal-focus state.
2. `Tui.cpp` performs the selected action and owns terminal lifecycle and rendering orchestration.
3. Panels and modals render domain-specific state without reading terminal input directly.
4. `VirtualTerminal.hpp` parses guest ANSI/UTF-8 output into a bounded screen and scrollback model.

## Input and focus

The dedicated UI thread exclusively owns stdin polling, escape-sequence parsing, terminal-size
updates, and drawing. Simulation threads enqueue guest UART bytes and request a render; they never
read stdin or render directly. This prevents concurrent consumers from dropping guest keystrokes or
splitting ANSI control sequences.

When the guest terminal is focused and running, ordinary bytes—including carriage return and
newline—are delivered directly to the platform UART (`ttyS0`). `Ctrl-P` pauses and `Ctrl-Q` quits.
While paused, keys control TUI navigation. An active modal
receives input before navigation, except global `Ctrl-R` reboot and `Ctrl-Q` quit. These controls
remain available after guest shutdown and wake the stopped simulation loop for clean teardown or
restart. The optional VirtIO console is a separate device and does not
receive copies of UART keystrokes.

The host terminal's carriage-return Enter byte is normalized to newline at the virtual-terminal
boundary, matching PTY line input and shells that read the UART without enabling `ICRNL` themselves.

Sessions start paused with TUI navigation owning the keyboard. Starting the simulator with `c`
also attaches the guest terminal; pausing detaches it. There is no independent focus state, so a
paused-but-attached or running-but-detached combination cannot occur. The terminal badge remains a
clickable state indicator.

Educational guidance is hidden by default. While paused, press `g` to toggle the compact guided
inspection strip. It remains hidden in short terminals so architectural state keeps priority.

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

`linux-boot-pty` is the end-to-end test for Linux boot and TUI/PTY/UART interaction. It runs the TUI
under a pseudo-terminal, waits for getty, sends Enter, and verifies a command executed by the guest
shell. Run the focused tests with:

```bash
ctest --test-dir build/rv64-release --output-on-failure -R 'tui-framework|linux-boot-pty'
```
