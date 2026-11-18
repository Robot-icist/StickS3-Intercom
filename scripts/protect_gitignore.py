"""Stop PlatformIO IDE from overwriting the project .gitignore.

PlatformIO IDE runs ``pio project init --ide vscode`` when the project is
activated or its IntelliSense index is rebuilt. Its non-optional ``.gitignore``
template rewrites the file from scratch, dropping custom entries. The IDE also
ships an optional template that is skipped when ``.gitignore`` already exists,
so disabling the non-optional one is enough to preserve the project's file.

PlatformIO updates can restore the template, so this pre-build script re-applies
the fix on every build. It never fails the build if the core install is
read-only; it only prints a warning in that case.
"""

from pathlib import Path

import platformio

Import("env")


def main():
    tpl_dir = (
        Path(platformio.__file__).parent
        / "project"
        / "integration"
        / "tpls"
        / "vscode"
    )
    active = tpl_dir / ".gitignore.tpl"
    disabled = tpl_dir / ".gitignore.tpl.disabled"

    if not active.is_file():
        return

    try:
        active.replace(disabled)
    except OSError as exc:
        print(
            "[protect_gitignore] Could not disable %s: %s. "
            "PlatformIO IDE may overwrite .gitignore again after an update."
            % (active, exc)
        )
        return

    print("[protect_gitignore] Disabled PlatformIO's .gitignore template: %s" % active)


main()
