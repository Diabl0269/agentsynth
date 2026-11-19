"""The scenario setting `audio: {rate, block, latency}`: starts the silent test device at that setup.

It becomes the `audioDeviceState` setting, a juce::AudioDeviceManager DEVICESETUP of the Silent device type that a
silent launch restores (Source/MainComponent/MainComponentSetup.cpp). Every key is optional.
"""

from scenario_checks import ScenarioError

DEFAULTS = {"rate": 44100, "block": 512, "latency": 0}
SETUP = ('<DEVICESETUP deviceType="Silent" audioOutputDeviceName="Silent test output" audioDeviceRate="%d" '
         'audioDeviceBufferSize="%d" silentOutputLatency="%d"/>')


def apply_audio_setting(settings, name="scenario"):
    """Returns a copy of `settings` where an `audio` mapping is replaced by the `audioDeviceState` XML string."""
    if "audio" not in settings:
        return dict(settings)
    audio = settings["audio"]
    if not isinstance(audio, dict):
        raise ScenarioError("%s: `settings.audio` must be a mapping of rate, block and latency" % name)
    unknown = sorted(set(audio) - set(DEFAULTS))
    if unknown:
        raise ScenarioError("%s: `settings.audio` takes rate, block and latency, not %s" % (name, ", ".join(map(str, unknown))))
    values = dict(DEFAULTS, **audio)
    for key, value in values.items():
        if isinstance(value, bool) or not isinstance(value, int) or value < (0 if key == "latency" else 1):
            raise ScenarioError("%s: `settings.audio.%s` must be a %s integer, got %r" % (
                name, key, "0 or more" if key == "latency" else "positive", value))
    result = {k: v for k, v in settings.items() if k != "audio"}
    result["audioDeviceState"] = SETUP % (values["rate"], values["block"], values["latency"])
    return result
