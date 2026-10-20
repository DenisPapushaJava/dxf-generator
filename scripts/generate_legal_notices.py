#!/usr/bin/env python3
"""Refresh embedded notices from the exact Qt source archive, without downloads."""
import argparse
import json
from pathlib import Path
import posixpath
import tarfile

QT_COMMIT = "7ddbc87d8e14ce51d2957ea72d0a6077593d5ff4"
QT_ROOT = f"qtbase-{QT_COMMIT}"
ROOT = Path(__file__).resolve().parents[1]


def generate(archive, output):
    output.mkdir(parents=True, exist_ok=True)
    sections = [
        "DXF Generator\nOwn application code: MIT.\n"
        "Uses Qt Core, Gui and Widgets, including the Windows platform plugin, under LGPLv3.\n"
        "Uses QXlsx under MIT. Third-party terms are separate from the application's MIT license.\n"
        "Source code, complete Qt sources and rebuilding instructions accompany releases:\n"
        "https://github.com/DenisPapushaJava/dxf-generator/releases\n"
        "Modified Qt builds may be relinked and run; no installation key or signature restriction applies.\n",
        "Application MIT license\n" + (ROOT / "LICENSE").read_text(),
        "QXlsx MIT license\n" + (ROOT / "third_party/qxlsx/LICENSE").read_text(),
        f"QtBase 6.10.3, commit {QT_COMMIT}\n"
        "The following is the complete attribution catalogue of the pinned Qt source tree.\n"
        "It also includes components/tools not enabled in our Windows configuration.\n",
    ]
    with tarfile.open(archive, "r:gz") as source:
        members = {m.name: m for m in source.getmembers() if m.isfile()}
        if not any(n.startswith(QT_ROOT + "/") for n in members):
            raise ValueError("Unexpected Qt archive root")
        def read(name):
            if not name.startswith(QT_ROOT + "/") or name not in members:
                raise ValueError("Missing license file: " + name)
            return source.extractfile(members[name]).read().decode("utf-8", errors="replace")
        for name in sorted(members):
            if not name.endswith("qt_attribution.json"):
                continue
            # Some upstream descriptions have literal newlines inside JSON strings.
            data = json.loads(read(name), strict=False)
            for item in data if isinstance(data, list) else [data]:
                sections.append(json.dumps(item, ensure_ascii=False, indent=2))
                paths = item.get("LicenseFile", [])
                for path in paths if isinstance(paths, list) else [paths]:
                    full = posixpath.normpath(posixpath.join(posixpath.dirname(name), path))
                    sections.append(f"License file: {full.removeprefix(QT_ROOT + '/')}\n" + read(full))
        # Preserve the complete SPDX license collection, not just linked LGPL text.
        for name in sorted(members):
            if name.startswith(QT_ROOT + "/LICENSES/"):
                sections.append(name.removeprefix(QT_ROOT + "/") + "\n" + read(name))
        for short in ("LGPL-3.0-only.txt", "GPL-3.0-only.txt"):
            (output / short).write_text(read(QT_ROOT + "/LICENSES/" + short), encoding="utf-8")
    notice = "\n\n" + ("\n\n" + "=" * 72 + "\n\n").join(sections)
    notice = "\n".join(line.rstrip() for line in notice.split("\n"))
    (output / "THIRD-PARTY-NOTICES.txt").write_text(notice, encoding="utf-8")
    print(f"Embedded notices: {len(notice.encode('utf-8')):,} bytes")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qt_archive", type=Path)
    args = parser.parse_args()
    generate(args.qt_archive, ROOT / "resources/licenses")
