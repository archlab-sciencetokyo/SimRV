# SimRV TUI Design Language

The SimRV TUI should communicate interaction before decoration. A filled background means a
control can be activated; plain text reports state. The framework lives in
`simrv::tui::framework`, while simulator-specific views remain in `simrv::tui`.

## Semantic roles

Use roles rather than named colors:

| Role | Meaning |
| --- | --- |
| `surface` | Modal or elevated background |
| `border` | Structural frames and rules |
| `text` | Primary passive copy |
| `muted` | Secondary copy, separators, inactive state |
| `value` | Machine data and inspected values |
| `accent` | Focus, navigation, and actionable controls |
| `success` | Valid, active, or completed state |
| `warning` | Changed, paused, or cautionary state |
| `danger` | Traps, failures, and invalid state |

Adaptive, Sakura, High Contrast, and Classic ANSI map these roles to their own palettes. Components
must obtain colors and glyphs from the active theme context.

## Components and interaction

- Filled keycaps, badges, and selected primary tabs are clickable.
- Passive status and telemetry remain plain text, grouped with a themed midpoint (`·`).
- A subordinate selected tab uses the accent role and underline; it does not compete with the
  filled category above it.
- Structural boundaries use theme box glyphs. Midpoints separate inline peers. Two terminal cells
  separate adjacent peer controls.
- Modal titles, tab rows, and action rows are centered. Modal body text, menus, and forms are
  left-aligned.
- Menu selection fills the label only; values remain readable as passive data.
- Scrollable regions show directional markers whenever content exists outside the viewport.
  Keyboard, mouse, drawing, and hit-testing must share the same offset and computed geometry.

## Layout contract

- Every renderer returns exactly its requested display width unless explicitly documented as a
  fragment builder.
- Width is measured in terminal cells, never UTF-8 bytes. ANSI control sequences have zero width.
- Rendering and pointer hit-testing consume the same `RenderedControl` spans.
- Narrow layouts reduce labels before removing important state. They must not hide controls behind
  passive text.
- Unicode themes may use box-drawing glyphs and arrows; Classic ANSI must remain complete with
  `+`, `-`, `|`, and ASCII direction labels.

## Examples

```text
Passive:    OS · IA                 Cycles 12.4M · Instruction Count 37.8M
Clickable:  [ PAUSED ]  [ SPEED MAX ]
Tabs:       [ 1 ] General · [ 2 ] MISA · [ 3 ] Microarchitecture
Scroll:     ◀ visible content ▶
```

Brackets in these examples represent background-filled terminal cells, not literal box characters.
