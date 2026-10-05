import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def check(host, fixture):
    provenance = json.loads((fixture / "provenance.json").read_text())
    source = fixture / "minimal.py"
    original = source.read_bytes()
    if hashlib.sha256(original).hexdigest() != provenance["sha256"]:
        raise AssertionError("Published plugin fixture has changed")
    result = subprocess.run(
        [str(host.resolve()), str(fixture.resolve())],
        capture_output=True, text=True, timeout=30,
    )
    output = result.stdout + result.stderr
    print(output, end="")
    if result.returncode:
        raise AssertionError(f"Embedded host exited with {result.returncode}")
    for marker in (
        "Plugin Host: loaded my_plugin (My Plugin, 1.0.0)",
        "plugin.my_plugin [INFO] My plugin loaded",
        "plugin.my_plugin [INFO] My plugin unloaded",
        "Plugin Host: unloaded my_plugin",
        "Plugin Host: CPython stopped",
    ):
        if output.count(marker) != 1:
            raise AssertionError(f"Expected exactly one occurrence: {marker}")
    if "failed:" in output:
        raise AssertionError("Plugin lifecycle failure")
    if source.read_bytes() != original:
        raise AssertionError("Plugin source changed during test")
    print("PASS: published ExteraGram lifecycle plugin, 0 changed lines, 0 shims")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("host", type=Path)
    parser.add_argument(
        "--fixture", type=Path,
        default=Path(__file__).parent / "fixtures" / "exteragram_minimal",
    )
    args = parser.parse_args()
    check(args.host, args.fixture)
