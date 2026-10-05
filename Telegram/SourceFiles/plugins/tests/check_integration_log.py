import argparse
import json
from pathlib import Path
import re


def check(log_path, manifest_path):
    manifest = json.loads(Path(manifest_path).read_text())
    text = Path(log_path).read_text(errors="replace")
    records = {kind: [] for kind in ("COMPAT", "SPEC")}
    for line in text.splitlines():
        match = re.search(r"plugin\.compatibility_logger \[INFO\] (COMPAT|SPEC) (\{.*\})$", line)
        if match:
            records[match[1]].append(json.loads(match[2]))
    sequences = [record["sequence"] for record in records["COMPAT"]]
    if not sequences or sequences != sorted(set(sequences)):
        raise AssertionError("Missing, repeated or unordered canonical update sequences")
    previous = 0
    for case in manifest["cases"]:
        expected = case["messages"]
        candidates = [record for record in records["COMPAT"]
                      if record["account"] == case["account"]
                      and record["update"] == case["update"]
                      and record["messages"] == expected]
        if len(candidates) != 1:
            raise AssertionError(f"{case['name']}: expected exactly one matching canonical update, got {len(candidates)}")
        update = candidates[0]
        if update["sequence"] <= previous:
            raise AssertionError(f"{case['name']}: event order differs from fixture actions")
        previous = update["sequence"]
        specialized = [record for record in records["SPEC"]
                       if record["sequence"] == update["sequence"]]
        callback = case.get("specialized")
        wanted = ([dict(message, account=case["account"], callback=callback,
                        sequence=update["sequence"]) for message in expected]
                  if callback else [])
        if specialized != wanted:
            raise AssertionError(f"{case['name']}: specialized callbacks differ from canonical snapshots")
        print(f"PASS {case['name']}: fields, batching, order, callback count")
    if "failed: compatibility_logger" in text:
        raise AssertionError("Compatibility logger callback failed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("log")
    parser.add_argument("manifest")
    args = parser.parse_args()
    check(args.log, args.manifest)
