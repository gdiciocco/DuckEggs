# Broad visual updates

User direction (2026-10-24): do not move PiP merely because content beneath or
around it updates when the update covers most of the visible work area. Moving
does not help when alternatives show the same broad update (scroll, page redraw,
or substantial text/content replacement).

Use bounded existing grayscale captures and portable core logic. This is a
spatial/temporal heuristic, not semantic recognition of reading, scrolling, or
text. Compare only pixels observable in both captures, excluding PiP and own UI;
advance the baseline even when evidence is ignored. Broad changes must not
become spatial avoidance heat or positive evidence of a quiet placement.

Suppress visual-only triggers for a bounded interval while broad evidence
continues, then recover for genuinely localized activity. Mouse proximity remains
an effective reason to evade; hovering controls and manual movement still freeze
or protect PiP as before. Preserve the12feature LinUCB/storage contract and
original Python. No screenshot persistence or extra runtime dependencies.

The broad detector can also start a bounded stable-content candidate, but it
never activates adaptation by itself. See `content-adaptation.md` for the
confirmation, overlay, and persistence rules.

Verification must distinguish synthetic detector/controller tests and parity
checks from live screen capture and actual PiP movement. Never claim a real
scroll/typing recognition test based solely on synthetic pixel patterns.
