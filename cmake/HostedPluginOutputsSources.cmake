# Hosted plugin output model and jack mapping (docs/architecture/plugin-layer.md),
# added to Core outside the main list in CMakeLists.txt, which sits at the repository's file-size cap.
target_sources(Core PRIVATE
    Source/Plugin/Hosting/HostedPluginModuleOutputs.cpp
)
