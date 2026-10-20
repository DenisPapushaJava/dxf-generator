# QXlsx

- Source: https://github.com/QtExcel/QXlsx
- Tag: `v1.5.1.1`
- Commit: `8a13e1c86e5d4fb5e3b2fb09c7b632514f1d54ca`
- License: MIT, see LICENSE.
- Vendored: QXlsx library headers, sources, CMake files; examples, tools and tests excluded.
- Local patches: skip CPack when embedded; use the OOXML numeric default for cells without a type; preserve malformed/empty numeric values instead of converting them to zero; concatenate inline rich-text runs; validate worksheet XML before parsing and propagate failures to document loading. Parent integration forces a static library. These protect dimensions, names and cached formula results from silent conversion/data loss.
- Uses Qt Core/Gui (already used by this application) and matching private Qt ZIP headers.
- No runtime Microsoft Excel, Python, network or additional shared-library dependency.

Binary-size impact is pending measurement at the next agreed Windows release checkpoint.
No Windows build is run as part of routine local development.
