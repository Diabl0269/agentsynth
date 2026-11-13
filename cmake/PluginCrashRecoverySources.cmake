# Naming the plugin a crash happened in on the next launch (docs/architecture/plugin-layer.md#when-a-plugin-takes-the-app-down),
# added to Core outside the main list in CMakeLists.txt, which sits at the repository's file-size cap.
target_sources(Core PRIVATE
    Source/Plugin/Hosting/CrashReportPluginFinder.h
    Source/Plugin/Hosting/CrashReportPluginFinder.cpp
    Source/Plugin/Hosting/PluginCrashRecovery.h
    Source/Plugin/Hosting/PluginCrashRecovery.cpp
)
include(cmake/HostedPluginOutputsSources.cmake)
