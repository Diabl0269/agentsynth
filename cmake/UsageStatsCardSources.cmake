# The usage statistics card slot (the question, its thanks and its collapse, shared by the Welcome screen and
# Settings > Preferences) and the Preferences card wiring. Split out of AppUISources.cmake, which is at the file-size cap.
target_sources(AppUI PRIVATE
    Source/UI/Chrome/UsageStatsCardSlot.cpp
    Source/UI/Chrome/UsageStatsCardSlot.h
    Source/UI/Settings/PreferencesSettingsTab/PreferencesSettingsTabUsageStatsCard.cpp
)
