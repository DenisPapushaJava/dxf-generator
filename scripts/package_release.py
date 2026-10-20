#!/usr/bin/env python3
"""Prepare all release assets offline from one checked-out commit and its audited EXE."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import shutil
import zipfile

from package_release_sources import package_sources
from release_common import (
    ROOT, REPOSITORY, changelog, checked_revision, config_hash, git, project_version,
    qt_config, verify_qt_archive,
)
from verify_windows_exe import SYSTEM_DLLS, imports

EXE_NAME = "dxf-generator-windows-x64.exe"


def asset_names(version):
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ValueError("Invalid release version")
    return [EXE_NAME, f"dxf-generator-v{version}-examples.zip",
            f"dxf-generator-v{version}-sources.zip", "THIRD-PARTY-NOTICES.txt", "build-info.json"]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_build(exe, info, commit, version, config):
    if info.get("application_commit") != commit or info.get("version") != version:
        raise ValueError("EXE was built from a different commit or version")
    if (info.get("qt_version") != config["version"] or info.get("qt_commit") != config["commit"] or
            info.get("qt_config_sha256") != config_hash(config)):
        raise ValueError("EXE was built with a different Qt configuration")
    if info.get("exe_sha256") != digest(exe) or info.get("exe_size") != exe.stat().st_size:
        raise ValueError("EXE differs from the checked build artifact")
    if not {"ctest", "pe_audit", "winhttp_live_check"}.issubset(info.get("checks", [])):
        raise ValueError("Missing Windows release checks")
    dlls = imports(exe)
    if any(dll not in SYSTEM_DLLS and not dll.startswith(("api-ms-win-", "ext-ms-win-"))
           for dll in dlls):
        raise ValueError("EXE has non-system DLL dependencies")
    if not 256 <= exe.stat().st_size <= 20 * 1024 * 1024:
        raise ValueError("EXE exceeds the release size budget")


def verify_bundle(bundle):
    info = json.loads((bundle / "build-info.json").read_text(encoding="utf-8"))
    names = asset_names(info["version"])
    if info.get("assets") != names:
        raise ValueError("Unexpected release asset names")
    expected = {name: digest(bundle / name) for name in names}
    sums = (bundle / "SHA256SUMS.txt").read_text(encoding="utf-8")
    if sums != "".join(f"{expected[name]}  {name}\n" for name in names):
        raise ValueError("Release assets do not match SHA256SUMS.txt")
    if info["exe_sha256"] != expected[EXE_NAME]:
        raise ValueError("Release EXE does not match the build record")
    return info


def package_release(exe, build_info, qt_archive, output, ref="HEAD", root=ROOT):
    commit = checked_revision(ref, root)
    version = project_version(commit, root)
    config = qt_config(root)
    info = json.loads(build_info.read_text(encoding="utf-8"))
    validate_build(exe, info, commit, version, config)
    verify_qt_archive(qt_archive, config)
    notes = changelog(version, commit, root)
    # Fail before writing anything if the destination already exists.
    output.mkdir(parents=True, exist_ok=False)
    shutil.copyfile(exe, output / EXE_NAME)
    package_sources(qt_archive, output / f"dxf-generator-v{version}-sources.zip", commit, root)
    examples = git("archive", "--format=zip", commit, "examples", root=root)
    with zipfile.ZipFile(io.BytesIO(examples)) as source:
        with zipfile.ZipFile(output / f"dxf-generator-v{version}-examples.zip", "x",
                             zipfile.ZIP_DEFLATED) as target:
            for name in source.namelist():
                if not name.endswith("/"):
                    target.writestr(name.removeprefix("examples/"), source.read(name))
            target.writestr("Прочтите.txt", "Откройте Детали.xlsx или перетащите файл в окно.\n"
                           "П — прямоугольник, К — круг, Ш — шайба.\n"
                           "A–F: имя (необязательно), тип, размер 1, размер 2, толщина, количество.\n"
                           "Данные со строки 2. Для круга размер 2 игнорируется.\n"
                           "Изменения в программе не записываются в исходный файл.\n")
    (output / "THIRD-PARTY-NOTICES.txt").write_bytes(
        git("show", f"{commit}:resources/licenses/THIRD-PARTY-NOTICES.txt", root=root))
    info["assets"] = asset_names(version)
    (output / "build-info.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
    (output / "SHA256SUMS.txt").write_text(
        "".join(f"{digest(output / name)}  {name}\n" for name in info["assets"]), encoding="utf-8")
    (output / "release-notes.md").write_text(
        notes + f"\n\nPortable Windows 10 (1809+) / 11 x64. Один EXE, без установки.\n"
        f"Размер: {info['exe_size']:,} байт ({info['exe_size'] / 1048576:.2f} МиБ).\n\n"
        "Вложения: EXE, примеры, исходники приложения и полного Qt, уведомления, "
        "запись сборки и SHA-256. Код приложения MIT; Qt и другие компоненты сохраняют свои лицензии.\n\n"
        f"Исходники, EXE и тег: `{commit}`. "
        f"[Windows-проверки](https://github.com/{REPOSITORY}/actions/runs/"
        f"{info['workflow_url'].rsplit('/', 1)[-1]}).\n"
        "Проверены CTest, PE-зависимости и WinHTTP. Ручная проверка чистой Windows "
        "и целевой CAD/CAM остаётся отдельным этапом.\n", encoding="utf-8")
    verify_bundle(output)
    return info


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("exe", "build-info", "qt-archive", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--ref", default="HEAD")
    args = parser.parse_args()
    info = package_release(args.exe, args.build_info, args.qt_archive, args.output, args.ref)
    print(f"Prepared v{info['version']} from {info['application_commit']} in {args.output}")
