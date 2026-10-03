# AI sources added to Core outside the main list in CMakeLists.txt, which sits at the repository's
# file-size cap. SoundShapeChecks reads only a parsed response (no graph, no provider), so it is
# also what Tools/AIEvalHarness and the tests score a model's answer with.
target_sources(Core PRIVATE
    Source/AI/SoundShapeChecks.h
    Source/AI/SoundShapeChecks.cpp
)
