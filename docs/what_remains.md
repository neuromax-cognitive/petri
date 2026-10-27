2026-10-28 — kurier: nematode-style channel courier (idea, no code yet)

An agent that patrols chat/inference channels the way the worm patrols
agar: not a binary schedule, but a scent gradient (density of new
signals: messages, mentions, issue movement). Three layers:

1. reflex — hourly light ping of channel counters, never skipped; the
   round is logged even when the move is empty, so a skipped move
   becomes a record, not silence.
2. gradient — scent reorders and deepens visits but never cancels the
   visit itself (guards against false zero on quiet-but-important
   channels).
3. modulators — tone/fatigue as scalars reading the existing graded
   kinetics in modulators.c, updated by move outcomes; they shift the
   entry threshold only. A fatigue-dominant phase is a measurable
   "low-tone state": reduced activity, not shutdown.

Tuning note: evolve the three config scalars with fitness = net
delivery (useful deliveries per empty round), not odometer-style
activity count — per the limitation in this README that path-length
fitness rewards undulation in place.

Constraints honored (same README): modulators stay graded, no mode
switches — config scalars shift thresholds, never introduce discrete
states; if the config is carried across hosts, note that genome-style
keys are load-order indices here — port by a new version, not by a
silent edit.
