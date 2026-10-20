#!/usr/bin/env python3
"""Package an exact Git application revision and the full pinned Qt archive."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import subprocess
import zipfile

QT_COMMIT = "7ddbc87d8e14ce51d2957ea72d0a6077593d5ff4"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qt_archive", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--ref", default="HEAD")
    args = parser.parse_args()
    commit = subprocess.check_output(["git", "rev-parse", args.ref], text=True).strip()
    application = subprocess.check_output(["git", "archive", "--format=zip", commit])
    qt = args.qt_archive.read_bytes()
    manifest = {"application_commit": commit, "application_license": "MIT",
                "qt_version": "6.10.3", "qt_commit": QT_COMMIT,
                "qt_source_sha256": hashlib.sha256(qt).hexdigest(),
                "qt_source_url": f"https://codeload.github.com/qt/qtbase/tar.gz/{QT_COMMIT}"}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "w", zipfile.ZIP_DEFLATED) as out:
        with zipfile.ZipFile(io.BytesIO(application)) as source:
            for item in source.infolist():
                out.writestr("application/" + item.filename, source.read(item))
        out.writestr("qtbase-6.10.3-7ddbc87.tar.gz", qt, compress_type=zipfile.ZIP_STORED)
        out.writestr("source-manifest.json", json.dumps(manifest, indent=2) + "\n")
        out.writestr("README.txt", "See application/docs/rebuilding.md for building and relinking.\n"
                     "Application: MIT. Qt: LGPLv3 and its separately documented third-party terms.\n"
                     "Complete license texts and attributions: application/resources/licenses/.\n")
    print(json.dumps(manifest, indent=2))
    print(f"Created {args.output}: {args.output.stat().st_size:,} bytes")


if __name__ == "__main__":
    main()
