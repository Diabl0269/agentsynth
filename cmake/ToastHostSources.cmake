# The toast host: owns the window's one toast and places it over the surface its source names. Split out of
# AppUISources.cmake, which is at the file-size cap.
target_sources(AppUI PRIVATE
    Source/MainComponent/ToastHost.cpp
    Source/MainComponent/ToastHost.h
)
# Chained here because CMakeLists.txt is at the file-size cap: the usage statistics card slot.
include(cmake/UsageStatsCardSources.cmake)
include(cmake/CanvasWindowSources.cmake) # chained for the same reason
