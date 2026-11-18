# README visuals

The README hero (`assets/readme/hero.webp`, a looping animated WebP) and the four gallery stills
(`timeline.png`, `mixer.png`, `piano-roll.png`, `midi-remote.png`) are captured by script, so they can be redone
whenever the UI changes.

```bash
pip install pillow
python3 Tools/ui-verify/examples/readme_visuals.py --app "<path to Agent Synth.app>" --out /tmp/readme
cp /tmp/readme/{hero.webp,timeline.png,mixer.png,piano-roll.png,midi-remote.png} assets/readme/
```

`--only hero` or `--only gallery` runs half of it. The script uses the [test mode](test-mode.md) client, so it
never touches the real mouse or keyboard, and it needs a build with `--automation`.

- **Hero**: a four-module chain (Oscillator, Filter, Delay, Audio Output) written by the script, library and
  minimap hidden, Show Response on, the audio pump running so the response curve is live. One drag on the Filter
  cutoff knob up and back, about 7 s at 12 fps, 1000 px wide, under 2 MB.
- **Gallery**: a four-track, 12-bar song and a virtual MiniLab 3 controller (from its template) mapped to the
  channel faders, pans and the filter, both built by the script. Each picture is a separate launch, cropped and
  reduced to a palette PNG under 400 KB.
- Look at every output before committing it: nothing half-drawn, no popup, no empty panel.
