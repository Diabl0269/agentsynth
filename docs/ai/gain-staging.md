# Gain staging of AI-built channels

A model sets module levels by feel, so some channels of an AI-made song peaked far over full scale (a Drum Kit at
+5 dBFS, a pad at +13) while others sat 20 dB lower, and the mixer meters showed red. Every fader started at 0 dB and
every VCA at 1.0, so the cause is the modules' own levels and how their voices add up, not a fader the model moved.

## The pass

After a batch that **added channel strips** (an edit plan or song build applied through `MainComponentTimelineOpsHost::recordBatch`):

1. `gainstaging::planWindows` picks where to measure. An arrangement of up to 640 beats (beat 0 to the last note's end,
   melodic tracks only) is rendered whole, in one window, so every peak of the song is heard. A longer one falls back
   to the busiest stretches: for each melodic track its four busiest 4-beat stretches (a note counts its velocity
   times the beats it sounds in the stretch), started up to 8 beats early so held notes are heard, merged where they
   touch and capped at 640 beats in all (the busiest stretch of every track is always kept). No notes, no render.
2. `gainstaging::measureChannelPeaks` renders those windows with the Export Stems path (`StemExporter`, 32-bit float,
   so overs are measured) and keeps each channel's loudest sample. This blocks the message thread for a few seconds.
3. `gainstaging::planTrims` lowers the fader of each **new** channel whose peak is over -3 dBFS until the peak sits
   0.5 dB under it (`kTrimMarginDb`: two renders of the same song differ by a few tenths of a dB, so aiming at exactly
   -3 could still read over on the next one), rounded down to the fader's 0.1 dB step. The fader is linear, so one measurement is enough. Nothing is ever raised, and
   channels that already existed are not touched.
4. The fader moves are recorded into the batch's own undo step, so one Cmd+Z removes the build and redo brings it back
   with its faders.

Only the faders move; module levels, VCA gains, sends and the Master are the model's and the user's. Measuring also never changes the
project: every parameter is put back after the render, since the song's automation and modulation move them. A song
too long to render whole is measured on its busiest stretches only, so a part that is loudest somewhere else can
still pass the target a little.
