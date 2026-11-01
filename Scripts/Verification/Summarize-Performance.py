"""Summarize raw route frames and opt-in production streaming wall scopes."""
import argparse
import csv
import json
import math
import re
from collections import defaultdict
from pathlib import Path


def distribution(values):
    v = sorted(values)
    if not v:
        return {"samples": 0}
    q = lambda p: v[max(0, math.ceil(len(v) * p) - 1)]
    return dict(samples=len(v), mean=sum(v) / len(v), median=q(.5), p95=q(.95),
                p99=q(.99), max=v[-1], above50=sum(x > 50 for x in v),
                above100=sum(x > 100 for x in v), above1000=sum(x > 1000 for x in v))


def summarize(path):
    result = json.loads((path / "verification.json").read_text(encoding="utf-8-sig"))
    groups = defaultdict(lambda: defaultdict(list))
    queues = defaultdict(list)
    with (path / "frames.csv").open(encoding="utf-8-sig", newline="") as f:
        for row in csv.DictReader(f):
            for column in ("wallFrameMs", "gameThreadMs", "renderThreadMs", "gpuMs", "drawCalls"):
                groups[row["kind"]][column].append(float(row[column]))
            for column in ("preparedSlots", "pendingDoodadTiles", "pendingLod1"):
                if column in row:
                    queues[column].append(int(row[column]))
    result["allFrames"] = {kind: {col: distribution(v) for col, v in cols.items()}
                           for kind, cols in groups.items()}
    scopes = defaultdict(list)
    for stage, ms in re.findall(r"StreamPerf stage=(\w+) ms=([\d.]+)", (path / "client.log").read_text(encoding="utf-8-sig")):
        scopes[stage].append(float(ms))
    result["scopesMs"] = {stage: distribution(v) for stage, v in scopes.items()}
    policy_log = (path / "client.log").read_text(encoding="utf-8-sig")
    if "StreamPolicy allWmoWorkers=1" in policy_log:
        if result["scopesMs"].get("wmoReadParse", {}).get("samples", 0):
            raise RuntimeError("A WMO group bypassed worker preparation")
        result["allWmoGroupsPreparedOnWorkers"] = True
    result["queueMaxima"] = {name: max(v) for name, v in queues.items()}
    if result["queueMaxima"].get("preparedSlots", 0) > 4:
        raise RuntimeError("More than four prepared/in-flight tile results")
    per_frame = defaultdict(int)
    log = (path / "client.log").read_text(encoding="utf-8-sig")
    budgets = re.findall(r"StreamBudget jobs=(\d+) terrain=(\d+) ms=([\d.]+) frame=(\d+)", log)
    stages = "finalizeTile|lod1|wdl" if budgets else "finalizeTile|m2Instances|lod1|wdl"
    for stage, frame in re.findall(r"StreamPerf stage=("+stages+r") ms=[\d.]+ frame=(\d+)", log):
        per_frame[stage, frame] += 1
        per_frame["all", frame] += 1
    if any(count > 1 for count in per_frame.values()):
        raise RuntimeError("More than one heavy unit of a kind in one engine frame")
    result["oneHeavyUnitPerKindPerFrame"] = bool(per_frame)
    if budgets:
        if any(int(j)>8 or int(t)>1 for j,t,ms,f in budgets) or len({f for j,t,ms,f in budgets})!=len(budgets):
            raise RuntimeError("Unbounded jobs, multiple terrain uploads or pumps in a frame")
        result["budgetedStreamingVerified"] = True
        result["queueMaxima"]["streamingJobsPerFrame"] = max(int(j) for j,t,ms,f in budgets)
        result["queueMaxima"]["terrainUploadsPerFrame"] = max(int(t) for j,t,ms,f in budgets)
        result["streamingBudget"] = {"cooperativeMs": 1, "maxJobs": 8,
             "batchMs": distribution([float(ms) for j,t,ms,f in budgets]),
             "note": "An indivisible job can overrun; no further job starts after the deadline."}
    (path / "summary.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(path.name, "cache:", result["cacheMode"])
    for step in result["steps"]:
        frame = step["ordinary"]["wallFrameMs"]
        print(f'{step["name"]:22} {step["secondsToSettled"]:6.2f}s '
              f'frame p95/p99/max {frame["p95"]:7.2f}/{frame["p99"]:7.2f}/{frame["max"]:7.2f} '
              f'private {step["privateMiB"]:7.1f}MiB tiles/sections {step["tiles"]}/{step["terrainSections"]}')
    for stage, v in result["scopesMs"].items():
        if stage != "wmoJob":
            print(f'{stage:22} n={v["samples"]} p95={v["p95"]:.2f} max={v["max"]:.2f}')
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("runs", type=Path, nargs="+")
    for run in parser.parse_args().runs:
        summarize(run)
