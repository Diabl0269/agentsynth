"""Unit tests for the pure parts of idle_cpu.py and cpu_threads.py: the settings file, the gap rule, the claim table,
the device read-back and the per-thread split from ps and from a sample call tree."""

import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import cpu_threads  # noqa: E402
import idle_cpu  # noqa: E402

PS_M = """USER         PID   TT   %CPU STAT PRI     STIME     UTIME COMMAND
tal        4242   ??   20.0 S    46T   0:01.00   0:02.00 /Applications/Agent Synth.app/Contents/MacOS/Agent Synth
           4242         9.0 S    97R   0:00.50   0:03.50
           4242         0.1 S    31T   0:00.10   0:00.20
"""

SAMPLE = """Analysis of sampling Agent Synth (pid 4242) every 1 millisecond
Call graph:
    1000 Thread_1   DispatchQueue_1: com.apple.main-thread  (serial)
    + 1000 start  (in dyld) + 1 [0x1]
    +   1000 main  (in Agent Synth) + 2 [0x2]
    +     700 mach_msg2_trap  (in libsystem_kernel.dylib) + 8 [0x3]
    +     300 juce::Component::paint(juce::Graphics&)  (in Agent Synth) + 4 [0x4]
    1000 Thread_2: com.apple.audio.IOThread.client
    + 1000 thread_start  (in libsystem_pthread.dylib) + 8 [0x5]
    +   1000 HALC_ProxyIOContext::IOWorkLoop()  (in CoreAudio) + 9 [0x6]
    +     800 semaphore_timedwait_trap  (in libsystem_kernel.dylib) + 8 [0x7]
    +     200 AudioEngine::audioDeviceIOCallbackWithContext(float const* const*)  (in Agent Synth) + 1 [0x8]
    +       150 AudioEngine::renderPass(juce::AudioBuffer<float>&)  (in Agent Synth) + 1 [0x9]
    1000 Thread_3: JUCE Timer
    + 1000 thread_start  (in libsystem_pthread.dylib) + 8 [0x5]
    +   1000 __psynch_cvwait  (in libsystem_kernel.dylib) + 8 [0xa]

Total number in stack (recursive counted multiple times):
"""


class IdleCpuTests(unittest.TestCase):
    def test_animations_are_pinned_never_left_to_follow(self):
        self.assertIn('name="animationMode" val="full"', idle_cpu.animations_settings_xml(True))
        self.assertIn('name="animationMode" val="off"', idle_cpu.animations_settings_xml(False))

    def test_the_device_setup_asks_for_the_buffer_on_the_named_output(self):
        setup = idle_cpu.device_setup_xml("MacBook Pro Speakers", 128)
        self.assertIn('deviceType="CoreAudio" audioOutputDeviceName="MacBook Pro Speakers"', setup)
        self.assertIn('audioDeviceBufferSize="128"', setup)
        self.assertNotIn("BufferSize", idle_cpu.device_setup_xml("MacBook Pro Speakers", 0))
        self.assertIn("audioDeviceState", idle_cpu.animations_settings_xml(True, setup))

    def test_the_opened_buffer_is_read_back_from_the_settings_file(self):
        text = ('<VALUE name="audioDeviceState">\n <DEVICESETUP deviceType="CoreAudio" audioOutputDeviceName="X" '
                'audioDeviceRate="44100.0"\n   audioDeviceBufferSize="512"/>\n</VALUE>')
        self.assertEqual(idle_cpu.opened_device(text), (512, 44100.0))
        self.assertEqual(idle_cpu.opened_device("<PROPERTIES/>"), (None, None))

    def test_the_default_output_device_is_found(self):
        class Out:
            stdout = ("Audio:\n\n    Devices:\n\n        Mic:\n          Input Channels: 1\n\n        MacBook Pro Speakers:\n"
                      "          Default Output Device: Yes\n")

        self.assertEqual(idle_cpu.default_output_device(run=lambda *a, **k: Out()), "MacBook Pro Speakers")

    def test_a_gap_over_a_quarter_is_a_finding(self):
        rows = [{"project": "Load test", "animations": "on", "real": 27.0, "test": 16.0},
                {"project": "new project", "animations": "on", "real": 5.0, "test": 4.5}]
        table = idle_cpu.table_markdown(rows).splitlines()
        self.assertIn("FINDING", table[2])
        self.assertIn("(ok)", table[3])

    def test_a_playing_row_has_its_thread_split_and_the_bench_share(self):
        real = {"project": "Load test", "animations": "on", "askedBuffer": 128, "openedBuffer": 128,
                "idle": {"median": 27.0, "split": {"audio": 18.0, "main": 7.0, "other": 2.0}},
                "playing": {"median": 50.0, "split": {"audio": 35.0, "main": 12.0, "other": 3.0}}}
        rows = idle_cpu.table_rows(real, {"median": 16.0}, {"idle": 5.2, "playing": 6.6})
        self.assertEqual([r["state"] for r in rows], ["idle", "playing"])
        self.assertIsNone(rows[1]["test"])
        table = idle_cpu.table_markdown(rows).splitlines()
        self.assertIn("| 128 (asked 128) | playing | 50.0% | 35.0% | 12.0% | 3.0% | - | - | 6.6 of 50.0 (13%) |",
                      table[3])
        self.assertIn("5.2 of 27.0 (19%)", table[2])

    def test_the_bench_share_is_block_time_over_real_time(self):
        self.assertAlmostEqual(idle_cpu.bench_share_percent(553), 5.18, places=2)
        self.assertEqual(idle_cpu.bench_line(None, 27.0), "-")

    def test_a_silent_real_launch_has_no_gap(self):
        self.assertEqual(idle_cpu.gap_fraction(0.0, 3.0), 0.0)

    def test_cpu_seconds_reads_days_and_minutes(self):
        class Out:
            stdout = "1-02:03:04.50"

        original = idle_cpu.subprocess.run
        idle_cpu.subprocess.run = lambda *a, **k: Out()
        try:
            self.assertAlmostEqual(idle_cpu.cpu_seconds(1), 86400 + 2 * 3600 + 3 * 60 + 4.5)
        finally:
            idle_cpu.subprocess.run = original


class CpuThreadsTests(unittest.TestCase):
    def test_ps_threads_parse_priority_and_cpu_time(self):
        self.assertEqual(cpu_threads.parse_ps_threads(PS_M), [(46, 3.0), (97, 4.0), (31, 0.3)])

    def test_the_split_names_main_audio_and_other(self):
        before = [(46, 3.0), (97, 4.0), (31, 0.3)]
        after = [(46, 4.0), (97, 6.0), (31, 0.4), (31, 0.5)]  # a thread started inside the window
        self.assertEqual(cpu_threads.split_between(before, after, 10.0), {"audio": 20.0, "main": 10.0, "other": 6.0})

    def test_a_sample_counts_only_samples_outside_a_wait(self):
        threads = cpu_threads.parse_sample(SAMPLE)
        self.assertEqual([t["busy"] for t in threads], [300, 200, 0])
        self.assertEqual(cpu_threads.sample_split(threads), {"audio": 20.0, "main": 30.0, "other": 0.0})
        self.assertEqual(cpu_threads.top_self(threads, "audio"),
                         [("AudioEngine::renderPass(juce::AudioBuffer<float>&)", 75.0),
                          ("AudioEngine::audioDeviceIOCallbackWithContext(float const* const*)", 25.0)])


if __name__ == "__main__":
    unittest.main()
