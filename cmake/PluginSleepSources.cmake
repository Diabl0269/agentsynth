# Unloading hosted plugins while the Mac sleeps (docs/architecture/plugin-layer.md#when-a-plugin-takes-the-app-down),
# added outside the main lists, which sit at the file-size cap. Core and AppUI both exist when this is included.
target_sources(Core PRIVATE
    Source/Plugin/Hosting/HostedPluginModuleSleep.cpp
)
target_sources(AppUI PRIVATE
    Source/MainComponent/MainComponentPluginSleep.cpp
    Source/Plugin/Hosting/HostedPluginSleepGuard.cpp
    Source/Plugin/Hosting/HostedPluginSleepGuard.h
    Source/Plugin/Hosting/SystemSleepWatcher.cpp
    Source/Plugin/Hosting/SystemSleepWatcher.h
    Source/UI/Settings/PreferencesSettingsTab/PreferencesSettingsTabPluginSleep.cpp
)
# The Mac sleep/wake notifications are Objective-C++, registered on the target like HostedPluginWindowMacKeyMonitor.mm.
if(APPLE)
    target_sources(AppUI PRIVATE Source/Plugin/Hosting/SystemSleepWatcherMac.mm)
    set_source_files_properties(Source/Plugin/Hosting/SystemSleepWatcherMac.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
endif()
