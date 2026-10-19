# Development rules

- Keep this a small offline C++20/Qt Widgets utility for Windows 10/11 x64, developed on Linux.
- The primary workflow is Excel list → row validation and preview → DXF files in a chosen folder. Prioritize this complete path over manual editing features. Project/kit files, CSV, nesting, materials and Undo/Redo are outside the current scope.
- DXF filenames are an interface consumed by CAM. Follow `docs/cam-filenames.md`: comma decimals independent of OS locale, optional source name, legacy field order and `(1)` duplicate suffixes. Do not silently sanitize identifiers or invent separators/suffixes.
- Manual creation has no name input: use automatic shape/dimension filenames. The optional custom name is only source metadata from Excel; keep it when editing imported parameters.
- `dxfgen_core` must have no Qt or other third-party dependencies. Geometry, validation and DXF serialization belong here, not in widgets.
- Preview and export must consume the same generated geometry. Dimensions are a preview overlay, never cutting geometry.
- Default configuration must not download dependencies. Add libraries only for implemented requirements; document purpose, version, license and binary-size impact.
- Do not overwrite existing user files silently. Validate all parts before starting a batch, and prevent paths from escaping the export directory.
- Keep builds and local toolchains in ignored directories. Never commit connector state, credentials or machine-specific paths.
- Verify changes with the relevant CMake presets and CTest. Windows single-EXE claims require a real static build and a successful PE import audit.
- The project license has not been selected. Do not add a license or change Qt licensing assumptions without the owner's decision.
