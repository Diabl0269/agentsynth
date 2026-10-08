# Decoding a project's samples and wavetables off the message thread while it opens on screen
# (docs/architecture/project-bundle.md#opening-a-project-on-screen), added to Core outside the main list in
# CMakeLists.txt, which sits at the repository's file-size cap.
target_sources(Core PRIVATE
    Source/Project/DeferredAssetLoads.h
    Source/Project/DeferredAssetLoads.cpp
)
