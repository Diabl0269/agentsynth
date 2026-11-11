# The Parametric EQ: 24 band slots, their filter design (types, cut slopes) and rendering. Part of Core; its own
# list so the root file stays under the line cap.
target_sources(Core PRIVATE
    Source/Modules/FX/ParametricEQModule.h
    Source/Modules/FX/ParametricEQ/ParametricEQBandTypes.h
    Source/Modules/FX/ParametricEQ/ParametricEQFilterDesign.h
    Source/Modules/FX/ParametricEQ/ParametricEQFilterDesign.cpp
    Source/Modules/FX/ParametricEQ/ParametricEQParamIds.h
    Source/Modules/FX/ParametricEQ/ParametricEQModule.cpp
    Source/Modules/FX/ParametricEQ/ParametricEQModuleProcess.cpp
)
