# Opt-in anonymous daily usage statistics (docs/development/usage-statistics.md). Part of Core; its own list so the
# root file stays under the line cap.
target_sources(Core PRIVATE
    Source/Telemetry/generated/Telemetry.g.h
    Source/Telemetry/TelemetryDay.h
    Source/Telemetry/TelemetryDay.cpp
    Source/Telemetry/TelemetryIdStore.h
    Source/Telemetry/TelemetryIdStore.cpp
    Source/Telemetry/TelemetryJson.h
    Source/Telemetry/TelemetryJson.cpp
    Source/Telemetry/TelemetryRecorder.h
    Source/Telemetry/TelemetryRecorder.cpp
    Source/Telemetry/TelemetrySender.h
    Source/Telemetry/TelemetrySender.cpp
    Source/Telemetry/TelemetryService.h
    Source/Telemetry/TelemetryService.cpp
    Source/Telemetry/UsageStatsChoice.h
    Source/Telemetry/UsageStatsChoice.cpp
)
