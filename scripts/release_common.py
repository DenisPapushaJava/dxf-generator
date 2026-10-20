"""Shared release metadata. Python standard library only; never downloads anything."""
import hashlib
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = "DenisPapushaJava/dxf-generator"


def git(*args, root=ROOT):
    return subprocess.check_output(["git", *args], cwd=root)


def qt_config(root=ROOT, commit=None):
    source = (git("show", f"{commit}:cmake/qt-static.json", root=root) if commit
              else (root / "cmake/qt-static.json").read_bytes())
    config = json.loads(source)
    if not re.fullmatch(r"\d+\.\d+\.\d+", config["version"]):
        raise ValueError("Invalid Qt version")
    if not re.fullmatch(r"[0-9a-f]{40}", config["commit"]):
        raise ValueError("Invalid Qt commit")
    if not re.fullmatch(r"[0-9a-f]{64}", config["source_sha256"]):
        raise ValueError("Invalid Qt archive checksum")
    if not config["configure_args"] or any(
        not re.fullmatch(r"[-A-Za-z0-9_=]+", arg) for arg in config["configure_args"]
    ):
        raise ValueError("Unsafe Qt configure argument")
    return config


def config_hash(config):
    return hashlib.sha256(json.dumps(config, sort_keys=True).encode()).hexdigest()


def archive_name(config):
    return f"qtbase-{config['version']}-{config['commit'][:7]}.tar.gz"


def archive_url(config):
    return f"https://codeload.github.com/qt/qtbase/tar.gz/{config['commit']}"


def verify_qt_archive(path, config):
    if hashlib.sha256(path.read_bytes()).hexdigest() != config["source_sha256"]:
        raise ValueError("Qt source archive does not match the pinned SHA-256")


def revision(ref="HEAD", root=ROOT):
    return git("rev-parse", "--verify", ref + "^{commit}", root=root).decode().strip()


def project_version(commit, root=ROOT):
    text = git("show", f"{commit}:CMakeLists.txt", root=root).decode()
    found = re.search(r"project\(DxfGenerator VERSION (\d+\.\d+\.\d+)\b", text)
    if not found:
        raise ValueError("Cannot read the project version")
    return found[1]


def checked_revision(ref="HEAD", root=ROOT):
    commit = revision(ref, root)
    if commit != revision(root=root):
        raise ValueError("Check out the exact release commit before building or packaging")
    if git("status", "--porcelain", "--untracked-files=normal", root=root).strip():
        raise ValueError("Release checkout must be clean (keep outputs in ignored build/)")
    return commit


def changelog(version, commit, root=ROOT):
    text = git("show", f"{commit}:CHANGELOG.md", root=root).decode("utf-8")
    found = re.search(
        rf"^## {re.escape(version)}(?:[ \t][^\n]*)?\n(.*?)(?=^## |\Z)",
        text, re.MULTILINE | re.DOTALL,
    )
    if not found or not found[1].strip():
        raise ValueError(f"CHANGELOG.md needs a nonempty section '## {version}'")
    return found[1].strip()
