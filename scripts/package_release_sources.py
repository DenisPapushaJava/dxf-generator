#!/usr/bin/env python3
"""Package an exact Git revision and the full, checksum-verified Qt source archive."""
import argparse
import io
import json
from pathlib import Path
import zipfile

from release_common import ROOT, archive_name, archive_url, git, qt_config, revision, verify_qt_archive


def package_sources(qt_archive, output, commit, root=ROOT):
    # Read the pin from the archived revision, not an unrelated local checkout.
    config = qt_config(root, commit)
    verify_qt_archive(qt_archive, config)
    application = git("archive", "--format=zip", commit, root=root)
    manifest = {
        "application_commit": commit, "application_license": "MIT",
        "qt_version": config["version"], "qt_commit": config["commit"],
        "qt_source_sha256": config["source_sha256"], "qt_source_url": archive_url(config),
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "x", zipfile.ZIP_DEFLATED) as out:
        with zipfile.ZipFile(io.BytesIO(application)) as source:
            for item in source.infolist():
                out.writestr("application/" + item.filename, source.read(item))
        out.write(qt_archive, archive_name(config), compress_type=zipfile.ZIP_STORED)
        out.writestr("source-manifest.json", json.dumps(manifest, indent=2) + "\n")
        out.writestr("README.txt", "See application/docs/rebuilding.md for building and relinking.\n"
                     "Application: MIT. Qt: LGPLv3 and its separately documented third-party terms.\n"
                     "Complete license texts and attributions: application/resources/licenses/.\n")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qt_archive", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--ref", default="HEAD")
    args = parser.parse_args()
    manifest = package_sources(args.qt_archive, args.output, revision(args.ref))
    print(json.dumps(manifest, indent=2))
    print(f"Created {args.output}: {args.output.stat().st_size:,} bytes")
