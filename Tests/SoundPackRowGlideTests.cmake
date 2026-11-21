# The Settings > Sound packs row glide and licence tests (docs/layout/animation.md#fading-things-in-and-out), kept in their own list
# so Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Settings/SoundPackRowGlideTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Settings/SoundPackRowLicenceTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Settings/SoundPackSourcesGlideTests.cpp
)
