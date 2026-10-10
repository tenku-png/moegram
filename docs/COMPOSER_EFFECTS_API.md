# Composer Effects API (MoeGram extension, version 1)

```python
from moegram.composer import ComposerEffects

self.set_composer_effects(ComposerEffects(
    insertion="pop", deletion="scatter", caret="smooth",
    send="fade_out", edit="soft", speed=1.0, intensity=1.0, style="bouncy",
    curve="auto",
))
```

`ComposerEffects` is an immutable value. Each effect defaults to `"none"`;
only the named effect above or `"none"` is accepted, except `insertion`,
which also takes letter motions `"drop"` (falls from above), `"zoom"` (grows
from zero) and `"spin"` (rotates in). `curve` picks the insertion easing:
`"auto"` (follows `style`), `"smooth"`, `"linear"`, `"cubic"`, `"back"`,
`"elastic"` or `"bounce"`. `deletion` also takes `"dust"`: a removed
letter breaks into falling dots (bounded pool of 160); the curves follow `Easing.java` from
text-animation by mihailkotovski (MIT). Speed must be finite and
between 0.5 and 2.0; intensity between 0.25 and 2.0. Invalid configurations
raise an exception before publication. `set_composer_effects(None)` removes
the registration. Registration is allowed during load and while loaded.

The Plugin Host delivers copied values to the Qt thread. Python receives no
Qt objects and has no frame callback. Native composer fields subscribe to a
C++ controller. If multiple plugins register effects, the lexicographically
smallest plugin ID owns the complete configuration. Disabling/unloading a
plugin removes its registration and clears transient state immediately.

**Typing Effects** (in the separate moe-plugins repository) is a normal plugin. Its ten controls
use the existing `ui.settings` implementation and per-plugin JSON storage.
Edit entry follows its insert switch. Disabling the plugin restores standard
input. The previous dev flag is no longer used.

Native rendering uses paint-only QTextEdit extra selections, cached glyph
runs, a bounded pool of 64 particles and one animation driver per field.
Logical text, cursor, selection, undo history and message sending are unchanged.
Small pastes (up to 48 visual clusters) stagger; larger pastes fall back
to native rendering. Send/edit exit snapshots are bounded to 4096 characters
and the visible viewport. Scroll, resize, hide, focus loss, chat/draft replacement,
unload and disabled animations clear transient state.

Unicode grapheme clusters (including combining marks, modifiers and ZWJ
sequences) animate as one unit. Standard emoji picker images use an immutable
native render snapshot; their normal paint cells are clipped only while the
snapshot animates. Custom emoji, IME, rich text and split ligatures use standard
rendering. No document text or formatting is changed for animation.

`style` accepts `"soft"`, `"pop"` (default) or `"bouncy"`. Bouncy uses a native
damped spring on insertion, stronger independent deletion particles and a lift
with scale overshoot on send. Speed/intensity remain numeric renderer values;
Typing Effects exposes selectors and migrates existing numeric settings to the
nearest choice on its first load. Stagger is capped at 12 steps to keep pastes
responsive. This API is a MoeGram extension, not an
ExteraGram API compatibility claim.
