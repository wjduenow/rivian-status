# PlatformIO pre-script: inject FW_VERSION from `git describe` into every build.
#
# Wired in via `extra_scripts = pre:tools/git_version.py` in [env] (platformio.ini), so all envs
# get it. The phase3 status page prints it, which is the only place a running build's provenance
# is visible over the network — that's what makes "is this device on the Release binary or on
# something I flashed from my laptop?" answerable without serial.
#
#   CI, tag build  -> "v0.1.0"                 (clean tree at a tag)
#   laptop build   -> "v0.1.0-3-gabc1234-dirty"
#   no tags yet    -> "abc1234" / "abc1234-dirty"  (--always falls back to the short hash)
#
# The `-dirty` / `-N-g<hash>` suffixes are the whole point: a Release binary is the only thing
# that can report a bare tag, so string-comparing against a manifest version can't be fooled by
# an ad-hoc build. Requires full history + tags in CI (`fetch-depth: 0`, `fetch-tags: true`).
import subprocess

Import("env")  # noqa: F821 — provided by PlatformIO's SCons environment

try:
    rev = (
        subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            cwd=env["PROJECT_DIR"],
            stderr=subprocess.DEVNULL,
        )
        .decode()
        .strip()
    )
except Exception:
    rev = "unknown"

# StringifyMacro wraps + escapes it into a C string literal, so FW_VERSION arrives as "v1.2-3-gabc".
env.Append(CPPDEFINES=[("FW_VERSION", env.StringifyMacro(rev))])
print("[git_version] FW_VERSION = %s" % rev)
