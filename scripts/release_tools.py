#!/usr/bin/env python3
"""Small CI helpers; GitHub writes are confined to the explicit draft command."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

from release_common import (
    ROOT, REPOSITORY, archive_name, archive_url, changelog, checked_revision,
    config_hash, project_version, qt_config,
)


def gh_json(endpoint):
    return json.loads(subprocess.check_output(["gh", "api", endpoint], text=True))


def all_releases():
    pages = json.loads(subprocess.check_output(
        ["gh", "api", "--paginate", "--slurp", f"repos/{REPOSITORY}/releases"], text=True))
    return [release for page in pages for release in page]


def ensure_new_release(version, commit, root=ROOT):
    # release-by-tag doesn't return drafts; list all pages to catch them too.
    if any(r["tag_name"] == f"v{version}" for r in all_releases()):
        raise ValueError("That version already has a release or draft; existing files are never replaced")
    tag = f"refs/tags/v{version}"
    result = subprocess.run(
        ["git", "ls-remote", "--exit-code", "--tags", "origin", tag, tag + "^{}"],
        cwd=root, text=True, capture_output=True,
    )
    if result.returncode not in (0, 2):
        raise ValueError("Cannot check the remote release tag: " + result.stderr)
    refs = dict(line.split()[::-1] for line in result.stdout.splitlines())
    target = refs.get(tag + "^{}", refs.get(tag))
    if target and target != commit:
        raise ValueError("Existing release tag points at a different commit")


def preflight(create_draft=False, reuse_ci=False, root=ROOT):
    commit = checked_revision(os.environ.get("GITHUB_SHA", "HEAD"), root)
    version = project_version(commit, root)
    if create_draft:
        changelog(version, commit, root)
        ensure_new_release(version, commit, root)
    runs = []
    if reuse_ci:
        candidates = gh_json(
            f"repos/{REPOSITORY}/actions/workflows/ci.yml/runs"
            f"?head_sha={commit}&status=success&per_page=20"
        )["workflow_runs"]
        # PR runs check a synthetic merge; their API head_sha can be the PR head.
        runs = [run for run in candidates if run.get("head_sha") == commit
                and run.get("conclusion") == "success"
                and run.get("event") in ("push", "workflow_dispatch")]
    return {"commit": commit, "version": version, "linux_required": str(not runs).lower()}


def qt_outputs():
    config = qt_config()
    compiler = os.environ.get("VCToolsVersion", "local").strip().replace(".", "-")
    return {
        "version": config["version"], "commit": config["commit"],
        "archive_name": archive_name(config), "archive_url": archive_url(config),
        "configure_flags": " ".join(config["configure_args"]),
        "cache_key": f"qtbase-{config['version']}-msvc-{compiler}-x64-{config_hash(config)[:16]}",
    }


def record_build(exe, output):
    commit = checked_revision(os.environ.get("GITHUB_SHA", "HEAD"))
    config = qt_config()
    info = {
        "application_commit": commit, "version": project_version(commit),
        "exe_sha256": hashlib.sha256(exe.read_bytes()).hexdigest(), "exe_size": exe.stat().st_size,
        "qt_version": config["version"], "qt_commit": config["commit"],
        "qt_config_sha256": config_hash(config),
        "msvc_tools_version": os.environ.get("VCToolsVersion", ""),
        "workflow_url": f"https://github.com/{REPOSITORY}/actions/runs/{os.environ.get('GITHUB_RUN_ID', '')}",
        "checks": ["ctest", "pe_audit", "winhttp_live_check"],
    }
    output.write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")


def create_draft(bundle):
    from package_release import verify_bundle
    manifest = verify_bundle(bundle)
    commit = checked_revision(os.environ.get("GITHUB_SHA", "HEAD"))
    version = project_version(commit)
    if manifest["application_commit"] != commit or manifest["version"] != version:
        raise ValueError("Bundle does not belong to this checkout")
    ensure_new_release(version, commit)
    assets = [bundle / name for name in manifest["assets"]] + [bundle / "SHA256SUMS.txt"]
    # No --clobber, no edit/publish API, and never --latest: always a new draft.
    subprocess.run([
        "gh", "release", "create", f"v{version}", "--repo", REPOSITORY,
        "--target", commit, "--draft", "--title", f"DXF Generator {version}",
        "--notes-file", str(bundle / "release-notes.md"), *map(str, assets),
    ], check=True)
    release = next(r for r in all_releases() if r["tag_name"] == f"v{version}")
    verify_uploaded_draft(release, bundle, manifest)


def verify_uploaded_draft(release, bundle, manifest):
    if (not release["draft"] or release["target_commitish"] != manifest["application_commit"]
            or release["tag_name"] != "v" + manifest["version"]):
        raise ValueError("Created draft has unexpected source metadata")
    expected = manifest["assets"] + ["SHA256SUMS.txt"]
    if sorted(a["name"] for a in release["assets"]) != sorted(expected):
        raise ValueError("Incomplete draft assets; inspect the draft before retrying")
    for asset in release["assets"]:
        path = bundle / asset["name"]
        if (asset["state"] != "uploaded" or asset["size"] != path.stat().st_size or
                asset.get("digest") != "sha256:" + hashlib.sha256(path.read_bytes()).hexdigest()):
            raise ValueError("GitHub asset digest does not match: " + asset["name"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("qt-outputs")
    before = commands.add_parser("preflight")
    before.add_argument("--create-draft", action="store_true")
    before.add_argument("--reuse-ci", action="store_true")
    record = commands.add_parser("record-build")
    record.add_argument("--exe", type=Path, required=True)
    record.add_argument("--output", type=Path, required=True)
    draft = commands.add_parser("create-draft")
    draft.add_argument("bundle", type=Path)
    args = parser.parse_args()
    if args.command == "qt-outputs":
        outputs = qt_outputs()
    elif args.command == "preflight":
        outputs = preflight(args.create_draft, args.reuse_ci)
    elif args.command == "record-build":
        record_build(args.exe, args.output)
        outputs = {}
    else:
        create_draft(args.bundle)
        outputs = {}
    for name, value in outputs.items():
        print(f"{name}={value}")
