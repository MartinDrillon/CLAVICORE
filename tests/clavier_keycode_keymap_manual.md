# Clavier Keycode and Keymap Manual Protocol

There is no host-side C test runner in this workspace. Validate the pure
keycode/keymap path through the Clavier USB serial output after building and
flashing as described in `clavier_matrix_scan_manual.md`.

1. Press and release positions 1, 61, and 72. The output must respectively
   identify `note 56`, `unused`, and `unused`.
2. Press position 84 (keycode `MAINT`) with a valid commissioning table present.
   The output must be `[MODE] maintenance=ON`, **not** a `behavior ... pressed`
   line: position 84 no longer carries the `A_fa` alteration (ADR-0006 §1). A
   second press must produce `[MODE] maintenance=OFF`. See
   `phase3b_maintenance.md` for the full campaign — and note that a
   `behavior MAINT pressed` line here means the dispatch of `main.c` is broken
   (ADR-0012).
3. Press and release position 76 three times. Each press must identify
   `behavior T_GlobUp pressed`; releases must not change behavior state.
4. Press and release position 73. The output must identify
   `behavior UNTRANSPOSE event=pressed`. Position 73 no longer carries an
   alteration code (ADR-0026); each press resets transpositions and
   alterations.
5. Press positions 77, 78, 79, and 80. The output must respectively identify
   `T_OctdownG`, `T_OctupG`, `T_OctupD`, and `T_OctdownD`.
6. Press and release position 74, then 75. The output must identify
   `behavior A_b event=pressed` then `behavior A_d event=pressed`, without a
   `state=` field and without any MIDI event: the modifiers act only while
   held, at the press of a NOTE. See `clavier_alteration_manual.md`.

The serial adapter intentionally logs base notes rather than a final transformed
MIDI note. The ordering between alteration and transposition resolution is
defined by ADR-0026 §4 (alteration computed before transposition) and reflected
in `docs/specs/clavier_keymap.json` (`alteration.resolution_order`).