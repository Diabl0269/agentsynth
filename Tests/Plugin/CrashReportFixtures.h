#pragma once

// An anonymised macOS crash report (`Agent Synth-<date>.ips`) for CrashReportPluginFinderTests: the shape of a
// real report (header line, then the body) from a crash on a plugin's own USB thread, trimmed to the keys the finder
// reads. Plugin, vendor and every path are invented; the user name is the system's own "USER" placeholder.

// clang-format off
namespace synth::test {

inline constexpr const char* kPluginCrashReport =
    R"CRASH({"app_name":"Agent Synth","timestamp":"2026-10-28 17:02:26.00 +0200","app_version":"0.202.0","bundleID":"com.agentsynth.app","os_version":"macOS 26.5.1 (25F80)","bug_type":"309"}
{
  "procName" : "Agent Synth",
  "exception" : {"type":"EXC_BAD_ACCESS","signal":"SIGSEGV","subtype":"KERN_INVALID_ADDRESS at 0xffffffffffffff98"},
  "faultingThread" : 2,
  "threads" : [
    {"id":1001,"queue":"com.apple.main-thread","frames":[
      {"imageOffset":4096,"symbol":"mach_msg2_trap","symbolLocation":8,"imageIndex":1},
      {"imageOffset":8192,"symbol":"juce::MessageManager::runDispatchLoop()","symbolLocation":52,"imageIndex":0}
    ]},
    {"id":1002,"name":"audio","frames":[
      {"imageOffset":4096,"symbol":"mach_msg2_trap","symbolLocation":8,"imageIndex":1}
    ]},
    {"id":1003,"triggered":true,"frames":[
      {"imageOffset":120560,"symbol":"pthread_mutex_lock","symbolLocation":12,"imageIndex":1},
      {"imageOffset":2200224,"imageIndex":2},
      {"imageOffset":2190112,"imageIndex":2},
      {"imageOffset":2182000,"imageIndex":2},
      {"imageOffset":2171904,"imageIndex":2},
      {"imageOffset":26636,"symbol":"_pthread_start","symbolLocation":136,"imageIndex":1}
    ]}
  ],
  "usedImages" : [
    {"source":"P","arch":"arm64","name":"Agent Synth","path":"/Applications/Agent Synth.app/Contents/MacOS/Agent Synth"},
    {"source":"P","arch":"arm64","name":"libsystem_pthread.dylib","path":"/usr/lib/system/libsystem_pthread.dylib"},
    {"source":"P","arch":"arm64","name":"Example Synth","path":"/Library/Audio/Plug-Ins/VST3/Example Synth.vst3/Contents/MacOS/Example Synth"}
  ]
})CRASH";

} // namespace synth::test
// clang-format on
