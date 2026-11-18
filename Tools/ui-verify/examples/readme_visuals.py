#!/usr/bin/env python3
"""Captures the README visuals: the hero loop (animated WebP) and the four gallery stills.

    python3 Tools/ui-verify/examples/readme_visuals.py [--app <Agent Synth.app>] [--out <dir>] [--only hero|gallery]

Needs Pillow (`pip install pillow`) for the WebP and the PNG optimisation; the rest is the standard test-mode client, so
nothing touches the real mouse or keyboard. The hero is built from a chain written here (Oscillator, Filter, Delay,
Audio Output) and filmed with the main window's own `screenshot` frames. Notes: docs/development/readme-visuals.md.
"""

import argparse
import json
import os
import shutil
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from agentsynth_testmode import TestModeApp, TestModeError  # noqa: E402

GALLERY_WIDTH = 1400  # px
GALLERY_COLOURS = 192  # PNG palette size
WIDTH = 1000  # px, the README's hero width
WEBP_QUALITY = 80
TURN_FRAMES = 24  # frames of the knob going up (and as many coming back)
TRAVEL_PX = 90  # how far the pointer drags the knob
SETTLE_S = 0.15
FRAME_MS = 80  # how long each frame of the turn shows
FIXTURES = os.path.join(os.path.dirname(HERE), "fixtures")


def load_project(name):
    with open(os.path.join(FIXTURES, name, "project.json")) as handle:
        return json.load(handle)


def write_project(path, data):
    os.makedirs(path, exist_ok=True)
    with open(os.path.join(path, "project.json"), "w") as handle:
        json.dump(data, handle, indent=2)
    return path


def hero_project(scratch):
    """Oscillator -> Filter -> Delay -> Audio Output, laid out left to right."""
    chain = load_project("dual-io-chain.agsproj")
    filt = [n for n in load_project("FRO1761 Filter card.agsproj")["nodes"] if n["type"] == "Filter"][0]
    nodes = {n["type"]: n for n in chain["nodes"]}
    osc, delay, out = nodes["Oscillator"], nodes["Delay"], nodes["Audio Output"]
    osc["id"], filt["id"], delay["id"], out["id"] = 2, 3, 4, 1
    osc["params"]["waveform"] = "Saw"
    filt["params"].update({"cutoff": 500.0, "resonance": 0.55})
    delay["params"].update({"mix": 0.25, "feedback": 0.35})
    for node, x in ((osc, 40), (filt, 375), (delay, 710), (out, 1045)):
        node["position"] = {"x": x, "y": 20}
    chain["view"] = {"zoom": 1.15, "panX": 0.0, "panY": 0.0}
    chain["nodes"] = [out, osc, filt, delay]
    chain["connections"] = [
        {"src": 2, "srcPort": 0, "dst": 3, "dstPort": 0, "isMidi": False},
        {"src": 3, "srcPort": 0, "dst": 4, "dstPort": 0, "isMidi": False},
        {"src": 4, "srcPort": 0, "dst": 1, "dstPort": 0, "isMidi": False},
    ]
    return write_project(os.path.join(scratch, "hero.agsproj"), chain)


def shot(app, path):
    app.call("screenshot", id="screen", path=path)
    return path


def hero(app, scratch, out):
    """Films one Cutoff turn up and back with Show Response on; returns the frame paths and their durations (ms)."""
    from PIL import Image

    app.call("openProject", path=hero_project(scratch))
    app.call("waitForIdle")
    for name in ("Hide Library", "Hide Minimap", "Show Response"):
        app.call("click", id=name)
        app.call("waitForIdle")
    app.call("audioPump", run=True)  # the response curve follows the cutoff the audio thread uses
    knob = "card:Filter > Cutoff"
    frames, step = [], [0]

    def grab(ms):
        path = os.path.join(scratch, "f%04d.png" % step[0])
        step[0] += 1
        shot(app, path)
        frames.append((path, ms))

    app.call("hover", id=knob)
    app.call("waitForIdle")
    grab(1200)
    centre = app.call("getState", id=knob)["bounds"]
    cx, cy = centre["w"] // 2, centre["h"] // 2
    app.call("mouseDown", id=knob, x=cx, y=cy)
    travel = [round(i * TRAVEL_PX / TURN_FRAMES) for i in range(1, TURN_FRAMES + 1)]
    for dy in travel + travel[-2::-1] + [0]:  # up, then back down to where it started
        app.call("mouseMoveHeld", id=knob, x=cx, y=cy - dy)
        time.sleep(SETTLE_S)  # the response curve repaints on a timer
        grab(FRAME_MS)
        if dy == travel[-1]:
            grab(1200)  # hold at the top
    app.call("mouseUp", id=knob, x=cx, y=cy)
    app.call("hover", id="card:Delay")
    app.call("waitForIdle")
    grab(800)
    print("cutoff now", app.call("getState", id=knob).get("value"))
    for path, _ms in frames:
        shutil.copy(path, os.path.join(out, 'frames', os.path.basename(path)))
    return frames


def write_webp(frames, path, width=WIDTH):
    from PIL import Image

    images = []
    for frame, _ms in frames:
        image = Image.open(frame).convert("RGB")
        images.append(image.resize((width, round(image.height * width / image.width)), Image.LANCZOS))
    images[0].save(path, save_all=True, append_images=images[1:], duration=[ms for _p, ms in frames], loop=0,
                   quality=WEBP_QUALITY, method=6)
    return os.path.getsize(path)


A_MINOR = [57, 60, 64, 67, 69, 72, 76]  # A3 C4 E4 G4 A4 C5 E5, the arp's pool
SONG_BPM = 100.0
SONG_BARS = 12
MIX = ((-5.0, -0.1), (-3.0, 0.0), (-8.0, 0.35), (-7.0, -0.3))  # (dB, pan) per track, so the faders differ


def note_list(start_id, items):
    """items: (startBeat, lengthBeats, pitch, velocity) -> the project's note records."""
    return [{"id": start_id + i, "startBeat": s, "lengthBeats": l, "pitch": p, "velocity": v, "channel": 1,
             "muted": False} for i, (s, l, p, v) in enumerate(items)]


def song_clips():
    """Four tracks of a small A minor loop: [(track name, [(clip name, startBeat, lengthBeats, notes)])]."""
    drums, bass, arp, chords = [], [], [], []
    for beat in range(16):
        drums.append((beat + 0.0, 0.25, 42, 70 + (beat % 2) * 20))  # hat on every beat
        drums.append((beat + 0.5, 0.25, 42, 60))
    for beat in (0, 3, 4, 7, 8, 10, 12, 15):
        drums.append((float(beat), 0.25, 36, 110))  # kick
    for beat in (2, 6, 10, 14):
        drums.append((float(beat), 0.25, 38, 100))  # snare
    for bar, root in enumerate((45, 41, 36 + 12, 43)):  # A F C G, one bar each
        bass += [(bar * 4.0, 1.5, root, 105), (bar * 4.0 + 1.5, 0.5, root, 85), (bar * 4.0 + 2.0, 1.0, root + 12, 95),
                 (bar * 4.0 + 3.0, 1.0, root, 90)]
    shape = (0, 2, 3, 4, 3, 2, 1, 2)  # indices into A_MINOR
    for step in range(32):
        arp.append((step * 0.5, 0.5, A_MINOR[shape[step % 8]] + (12 if step % 16 >= 8 else 0), 80 + 20 * (step % 4 == 0)))
    for bar, triad in enumerate(((57, 60, 64), (53, 57, 60), (55, 60, 64), (55, 59, 62))):
        chords += [(bar * 4.0, 3.5, pitch + 12, 75) for pitch in triad]
    return [
        ("Kit", [("Beat %d" % (i + 1), 16.0 * i, 16.0, drums) for i in range(3)]),
        ("Bass", [("Bass line %d" % (i + 1), 16.0 * i, 16.0, bass) for i in range(3)]),
        ("Arp", [("Arp %d" % (i + 1), 16.0 * i, 16.0, arp) for i in range(3)]),
        ("Keys", [("Chords %d" % (i + 1), 16.0 * i, 16.0, chords) for i in range(3)]),
    ]


def song_project(app, scratch):
    """Adds the four instrument tracks the way a user does, saves, then fills their clips in the saved file."""
    app.call("click", id="New empty project")
    app.call("waitForIdle")
    app.call("key", keys="cmd+j")
    app.call("waitForIdle")
    for kind in ("Drum Kit", "Oscillator", "Pluck", "Mallet"):
        app.call("click", id="timelineAddTrackButton")
        app.call("waitForIdle")
        app.call("click", id="modMatrixPicker > %s, Track" % kind)
        app.call("waitForIdle")
        app.call("key", keys="escape")
        app.call("waitForIdle")
    path = os.path.join(scratch, "song.agsproj")
    app.call("saveProject", path=path)
    with open(os.path.join(path, "project.json")) as handle:
        data = json.load(handle)
    clip_id = note_id = 1
    for track, (name, clips) in zip(data["timeline"]["tracks"], song_clips()):
        track["name"] = name
        track["clips"] = []
        for clip_name, start, length, notes in clips:
            track["clips"].append({"id": clip_id, "name": clip_name, "startBeat": start, "lengthBeats": length,
                                   "muted": False, "notes": note_list(note_id, notes)})
            clip_id += 1
            note_id += len(notes)
    data["timeline"].update({"nextClipId": clip_id, "nextNoteId": note_id})
    data["transport"]["bpm"] = SONG_BPM
    for macro, name in zip(data["macros"], ("Kit", "Bass", "Arp", "Keys")):
        macro["name"] = name  # the Mixer strip header reads the macro's name
    strips = [n for n in data["nodes"] if n["type"] == "Channel Strip"]
    for strip, (gain, pan) in zip(strips, MIX):
        strip["params"].update({"gain": gain, "pan": pan})
    write_project(path, data)
    return path


def showing(app, component_id):
    try:
        return bool(app.call("getState", id=component_id).get("showing"))
    except TestModeError:
        return False


def tidy(app, project):
    """Opens the project with the library hidden and the bottom panel open; each is only toggled when it is not
    already in that state."""
    app.call("openProject", path=project)
    app.call("waitForIdle")
    if showing(app, "Hide Library"):
        app.call("click", id="Hide Library")
        app.call("waitForIdle")
    if not showing(app, "Show Timeline in the main area") and not showing(app, "Show Mixer in the main area"):
        app.call("key", keys="cmd+j")
        app.call("waitForIdle")


def uuid_of(data, node_type, nth=0):
    return [n["uuid"] for n in data["nodes"] if n["type"] == node_type][nth]


def controller_project(app, scratch, song):
    """Adds a virtual MiniLab 3 controller to the song (Add controller, then Templates), saves it and maps its
    faders to the four channel faders and its knobs to the pans, so every cell of the surface names a target."""
    tidy(app, song)
    app.call("click", id="Show Controllers in the main area")
    app.call("waitForIdle")
    time.sleep(1.5)
    app.call("click", id="addControllerButton")
    app.call("waitForIdle")
    time.sleep(0.8)
    for _ in range(30):
        if app.call("getState", id="addControllerDeviceCombo")["text"].startswith("No device"):
            break
        app.call("key", keys="down", target="addControllerDeviceCombo")
    app.call("click", id="addControllerOk")
    app.call("waitForIdle")
    time.sleep(1)
    app.call("click", id="window", x=440, y=59)  # Templates
    time.sleep(0.8)
    for _ in range(6):  # 8 knobs, 8 faders + 8 buttons, Transport strip, Keyboard with 8 knobs, BeatStep, MiniLab 3
        app.call("key", keys="down")
    app.call("key", keys="return")
    time.sleep(1)
    path = os.path.join(scratch, "song-controller.agsproj")
    app.call("saveProject", path=path)
    profile_file = [f for f in os.listdir(os.path.join(app.settings_dir, "MidiRemote", "Controllers"))
                    if f != "mackie-control.json"][0]
    with open(os.path.join(app.settings_dir, "MidiRemote", "Controllers", profile_file)) as handle:
        profile = json.load(handle)
    with open(os.path.join(path, "project.json")) as handle:
        data = json.load(handle)
    controls = {c["name"]: c for c in profile["controls"]}
    strips = [uuid_of(data, "Channel Strip", i) for i in range(4)]
    wanted = [("Fader %d" % (i + 1), strips[i], "gain") for i in range(4)]
    wanted += [("Knob %d" % (i + 1), strips[i], "pan") for i in range(4)]
    wanted += [("Knob 5", uuid_of(data, "Filter"), "cutoff"), ("Knob 6", uuid_of(data, "Filter"), "resonance")]
    assignments = []
    for index, (control_name, node, param) in enumerate(wanted):
        control = controls[control_name]
        assignments.append({
            "id": "readme-assignment-%d" % index,
            "control": {"profileId": profile["id"], "controlId": control["id"]},
            "spec": control["message"], "specEncoding": control["encoding"],
            "specButtonMode": control["buttonMode"], "specControlName": control_name,
            "target": {"parameter": {"nodeUuid": node, "paramId": param, "paramIndexHint": -1}},
            "takeover": "default", "range": {"min": 0.0, "max": 1.0}, "enabled": True})
    data["midiRemote"]["assignments"] = assignments
    write_project(path, data)
    return path


def save_still(frame, path, crop=None, width=GALLERY_WIDTH, colours=GALLERY_COLOURS):
    """Writes a captured frame as an optimised PNG: cropped, scaled to `width` and reduced to a palette."""
    from PIL import Image

    image = Image.open(frame).convert("RGB")
    if crop:
        image = image.crop(crop)
    image = image.resize((width, round(image.height * width / image.width)), Image.LANCZOS)
    image.quantize(colors=colours, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).save(path, optimize=True)
    return os.path.getsize(path)


def play(app, seconds):
    """Runs the audio pump and the transport so the meters and the playhead are alive, then lets `seconds` pass."""
    app.call("audioPump", run=True)
    app.call("key", keys="space")
    time.sleep(seconds)


def gallery(settings_dir, scratch, project, out):
    """One launch per picture, each opened the way a user gets there. Returns {name: (frame path, crop box or None)}."""
    frames = {}

    def launch():
        # The dock layout is a saved setting, so every picture starts from its own copy of the same settings.
        fresh = tempfile.mkdtemp(prefix="readme-visuals-shot-")
        shutil.rmtree(fresh)
        shutil.copytree(settings_dir, fresh)
        return TestModeApp(settings_dir=fresh, extra_args=("--automation-motion",))

    def close_panel(app):
        app.call("key", keys="cmd+j")
        app.call("waitForIdle")
        time.sleep(1.0)

    def take(app, name, crop=None):
        app.call("hover", id="window", x=800, y=20)  # the pointer on the empty toolbar, so no tooltip shows
        time.sleep(0.6)
        path = os.path.join(scratch, name + ".png")
        shot(app, path)
        frames[name] = (path, crop)

    with launch() as app:
        tidy(app, project)
        app.call("click", id="Show Timeline in the main area")
        app.call("waitForIdle")
        close_panel(app)
        play(app, 2.5)
        take(app, "timeline", crop=(0, 44, 1600, 400))
    with launch() as app:
        tidy(app, project)
        app.call("click", id="Show Mixer in the main area")
        app.call("waitForIdle")
        close_panel(app)
        play(app, 3.0)
        take(app, "mixer", crop=(0, 44, 1600, 875))
    with launch() as app:
        tidy(app, project)
        app.call("click", id="Show Timeline in the main area")
        app.call("waitForIdle")
        time.sleep(1.2)
        app.call("click", id="timelineClipLaneArea", x=120, y=30 + 2 * 56, clicks=2)  # the Arp track's first clip
        app.call("waitForIdle")
        time.sleep(1.2)
        close_panel(app)
        take(app, "piano-roll", crop=(0, 44, 1600, 560))
    with launch() as app:
        tidy(app, project)
        app.call("click", id="Show Controllers in the main area")
        app.call("waitForIdle")
        time.sleep(1.5)
        app.call("click", id="window", x=70, y=59)  # the controller in the list
        time.sleep(0.8)
        app.call("click", id="window", x=234, y=135)  # Knob 1, so the inspector shows its mapping
        app.call("waitForIdle")
        take(app, "midi-remote", crop=(0, 44, 1600, 380))
    return frames


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app")
    parser.add_argument("--out", default=os.path.join(os.path.dirname(HERE), "out", "readme"))
    parser.add_argument("--only", choices=("hero", "gallery"))
    options = parser.parse_args()
    out = os.path.abspath(options.out)
    os.makedirs(os.path.join(out, "frames"), exist_ok=True)
    scratch = tempfile.mkdtemp(prefix="readme-visuals-")
    settings_dir = tempfile.mkdtemp(prefix="readme-visuals-settings-")
    try:
        if options.only != "gallery":
            with TestModeApp(options.app, extra_args=("--automation-motion",)) as app:
                frames = hero(app, scratch, out)
            size = write_webp(frames, os.path.join(out, "hero.webp"))
            print("hero.webp: %d frames, %d KB" % (len(frames), size // 1024))
        if options.only != "hero":
            with TestModeApp(options.app, settings_dir=settings_dir) as app:
                song = song_project(app, scratch)
            with TestModeApp(options.app, settings_dir=settings_dir, extra_args=("--automation-motion",)) as app:
                project = controller_project(app, scratch, song)
            for name, (frame, crop) in gallery(settings_dir, scratch, project, out).items():
                shutil.copy(frame, os.path.join(out, "frames", name + ".png"))
                size = save_still(frame, os.path.join(out, name + ".png"), crop)
                print("%s.png: %d KB" % (name, size // 1024))
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
        shutil.rmtree(settings_dir, ignore_errors=True)


if __name__ == "__main__":
    main()
