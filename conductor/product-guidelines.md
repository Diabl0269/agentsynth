# Product Guidelines

## Core Principles
1. **Performance First:** All audio-related code must be real-time safe. Avoid heap allocation, locks, or heavy syscalls in the audio processing callback.
2. **Modular Integrity:** Modules should be loosely coupled and follow the established `ModuleBase` interface.
3. **User Experience:** The visual interface should be intuitive, responsive, and provide clear feedback during patching and modulation.
4. **Consistency:** Maintain naming, structural, and styling conventions throughout the codebase as defined in `GEMINI.md`.

## Development Style
- **C++20 Standards:** Utilize modern C++ features for safer and more expressive code.
- **JUCE Best Practices:** Follow JUCE framework guidelines for component handling, audio processing, and asset management.
- **Documentation:** Every new module and significant change must be documented in the relevant `docs/` file.
- **Testing:** New functionality requires corresponding tests in the `Tests/` directory, aiming for at least 85% coverage.

## UI/UX Principles
- **Visual Clarity:** Connections, modulation routing, and parameter changes should be visually distinct.
- **Accessibility:** Ensure the interface is navigable and readable, with clear labels and intuitive control behaviors.
- **Feedback:** Provide immediate visual feedback for all user actions (e.g., parameter adjustments, cable connections).

## Contribution Philosophy
- **Open Source:** Adhere to the MIT license and foster an open, collaborative environment.
- **Quality Assurance:** All PRs must pass the CI pipeline (linting, building, testing) before merging.
- **Community:** Encourage feedback, bug reports, and contributions that align with the core mission.
