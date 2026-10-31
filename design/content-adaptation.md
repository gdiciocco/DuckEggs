# Bounded content adaptation

Phase 1 detects a stable content transition within the current app/role/layout
and rotates one temporary RAM activity view. It does not identify documents,
screens, titles, URLs, or text, and it does not persist a content fingerprint.

## Detector

The existing 64x36 grayscale frame is reduced to its 16x9 cell means. A
transient structural signature stores each mean as a 4-bit level (144 bytes)
plus the cells observable in that frame. PiP, helper, and other captured
exclusions are omitted, and comparisons use only cells valid in both frames.

- Broad motion arms content detection but never confirms a change. Every
  observation classified as currently broad resets the candidate and its
  settling evidence, even when quantized cell means happen to resemble one
  another. A candidate can only accumulate on fresh, consecutive non-broad
  observations after the broad motion stops.
- At least 60% of the 16x9 cells must be comparable. The current signature must
  differ from the previous stable signature by at least two levels in at least
  40% of comparable cells, across at least half of the columns and rows.
- Candidate observations must remain comparable and at least 90% of their
  cells must stay within one level of the candidate. Three fresh observations
  over at least 0.5 seconds are required; observations more than 1 second
  apart restart the candidate.
- A 0.75 second cooldown after confirmation prevents one settled redraw from
  producing repeated transitions. Continuous scrolling or broad video motion
  keeps resetting candidates through the broad-motion veto and therefore does
  not confirm while the content is moving.
- A capture gap over 1.5 seconds, a capture geometry change, or an
  app/role/layout context change resets the candidate and adopts a fresh stable
  reference without reporting a transition. The capture-gap rebaseline also
  refreshes the last valid-content time so detection can resume after the gap.
  Short animation or repeated idle frames remain on the current stable content.

On confirmation, the tracker stores the new compact signature as the stable
reference and reports a one-shot transition. A later A-to-B-to-A sequence can
confirm both settled changes; this is re-adaptation, not document identity
recognition.

The signature is deliberately coarse. Changes that preserve the same 16x9 cell
means, or fall below the quantized/change thresholds, can remain
indistinguishable. This is a conservative visual heuristic rather than
semantic content recognition.

## Temporary activity overlay

The durable profile still owns the saved 24x14 short, long, and manual maps.
Its observation and natural-decay behavior is unchanged: cursor dwell,
movement, clicks, input activity, and caret observations continue through the
existing profile path. The manual layer is never overlaid.

While content adaptation is active, a fixed-size RAM-only overlay collects new
short/long activity with 3 second and 30 second half-lives. Overlay attribution
requires observable interaction evidence inside the foreground reference and
outside the PiP and helper regions: cursor movement, a new click, recent
`input_active` with a valid `input_age`, or an active-input caret. A stationary
cursor and the elapsed interval before the overlay began are not attributed as
new interaction evidence.

- durable short/long multiplied by a baseline weight that starts at 25% and
  fades toward 5% with an 8 second time constant as bounded post-transition
  interaction exposure accumulates, plus the overlay values;
- the durable manual map, unchanged.

Idle wall time does not reduce the baseline weight. The overlay itself continues
to use its short/long half-lives, while interaction exposure is accumulated
only over valid post-transition evidence intervals.

Placement scoring, live decision features, and active-profile insights all read
this effective view. The overlay is never written to SQLite, its blended values
never replace the durable maps, and the 12-feature LinUCB matrix/update formula
and storage schema are unchanged. No image change generates a reward.

A confirmed transition invalidates pending and moving outcomes without a quiet,
correction, or movement-failure penalty. The overlay is replaced on the next
confirmed transition and cleared on an actual app/role/layout or PiP identity
change. Pause, capture gaps, helper interaction, and the existing broad
suppression continue to gate observation and movement as before. New
overlay evidence stays only in RAM until the next actual context switch or a
new confirmed transition; durable evidence continues through its normal path.

The structural signature, candidate, and overlay are all fixed-size and
in-memory; there is no persistent content-subprofile bank or unbounded cache.
Phase 1 does not require a persistent context bank, document identity, or any
identity-recognition model.
For diagnosis, controller status adds `content_overlay_active` only while the
overlay exists and `content_transition_count` only after the first confirmed
transition. Neither key is added to ordinary pre-adaptation status, preserving
the existing status/parity shape.

## Verification boundary

Core tests use synthetic grayscale frames to cover settled A-to-B-to-A,
continuous scrolling, localized motion, masks, capture gaps, geometry/context
changes, effective scoring/insights, persistence, and pending-outcome
censoring. These tests do not prove semantic recognition of real scrolling,
reading, video, or title changes, and they do not exercise live capture or PiP
movement.

## Movement regression guardrails

The first content-adaptation build called the general opportunity invalidator
when a transition started. That reset a committed motion as well as its pending
outcome, including cursor escapes that were already under way. Separately, the
pending outcome was settled before motion advancement and treated a cursor
within 100 px of the selected target as a return even when the PiP had not
arrived. The latter could cancel a cramped-workspace escape after about 0.12 s,
leaving only a small part of the occupied area cleared.

Transition activation now detaches the pre-transition pending outcome and its
return timer without touching committed motion, route progress,
own-interpolation bookkeeping, the last emitted rectangle, or ordinary negative
feedback after a broad redraw. A completed pre-transition move therefore cannot
earn a stale quiet reward or accept a stale manual correction. Return feedback
still requires actual arrival at the selected target before its approach timer
starts.

Cursor-triggered placement initially requires a safe route whose distance from
the cursor to the full PiP rectangle does not decrease beyond a one-pixel
integer-rounding tolerance. Route eligibility is established before the 65%
clearance goal and fallback ranking, so an unreachable clearing target cannot
hide a valid monotonic fallback. Strict clearance remains active while the
pointer is near the escape source or current PiP. If the user moves the pointer
away, the already committed motion may finish along any geometrically safe
route instead of being cancelled relative to the new pointer position. Dynamic
collision checks, hover/button holds, helper exclusions, and manual corrections
remain unchanged. Sustained-visual placement retains its existing route and
clearance fallback behavior.

PiP control hover uses the actual window rectangle, including its boundary,
with no outside slop. The separate 20 px visual-capture mask remains unchanged.
An outside approach closer than the 28 px safety corridor triggers immediately;
approaches farther away retain the short dwell. If the pointer is already
inside that corridor when movement starts, cursor escape may use an opt-in
recovery route only when continuous full-rectangle pointer clearance is
nondecreasing and the rectangle never covers or crosses the pointer. That
route remains revalidated until normal 28 px clearance is recovered. Other
geometry callers keep the strict default. The Windows source observes cursor
position and buttons, not cursor shape, so the hand-over-link case is
shape-independent; live pointer behavior has not been hardware-verified here.

Regression coverage uses static content for side, top, bottom, and corner
approaches with a biased 12-feature pretrained model and manual preference, plus
a larger PiP in a smaller work area. It applies each emitted position to a fake
backend until arrival and checks nondecreasing pointer clearance and material
area clearing, or a legitimate no-safe-target result. One trace moves the
pointer away after the first emitted step and verifies that the committed escape
still reaches the clearance goal without a movement failure or reward. An
A-to-B transition is also injected into an active cursor escape to verify target
continuity, detached old feedback, and a later recent-input observation near
the actual target. A matched two-cell localized sequence at the native-like
0.5-second capture cadence is run with and without adaptation and must trigger
sustained-visual movement that reaches the clearance goal. Capture is omitted
while a move is active, matching the real runtime suppression; masks follow the
observed PiP geometry.

These are synthetic controller contracts. They do not establish that a
particular real page, video, compositor, or PiP window will produce those pixel
patterns. Windows capture gating, native `SetWindowPos` movement, DPI scaling,
and real pointer timing still require hardware validation.
