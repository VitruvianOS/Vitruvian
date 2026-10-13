#!/usr/bin/env python3
# Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
# All rights reserved. Distributed under the terms of the MIT License.
#
# Seeded, reproducible fuzzer for the installer's partition-plan engine
# against file-backed disks. Every case is derived from one printed
# seed; a failing case is replayed with --seed <base> --cases <n>.

import argparse
import hashlib
import json
import os
import random
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SIM = HERE
HELPER_FUNCS = os.path.join(SIM, "work", "helper-functions-test.sh")
APPLY = os.path.join(SIM, "work", "drive-apply.sh")

FILESYSTEMS = ["ext4", "xfs", "btrfs", "fat32", "swap"]


def run(cmd, **kwargs):
    return subprocess.run(cmd, capture_output=True, text=True, **kwargs)


def table_digest(path):
    out = run(["sfdisk", "--dump", path]).stdout
    flat = " ".join(out.split())
    return hashlib.md5(flat.encode()).hexdigest()


def partitions(path):
    out = run(["sfdisk", "--json", path])
    if out.returncode != 0:
        return []
    try:
        data = json.loads(out.stdout)
    except json.JSONDecodeError:
        return []
    return data.get("partitiontable", {}).get("partitions", [])


def region_md5(path, start_sector, size_sector):
    h = hashlib.md5()
    with open(path, "rb") as f:
        f.seek(start_sector * 512)
        remaining = size_sector * 512
        while remaining > 0:
            chunk = f.read(min(1 << 20, remaining))
            if not chunk:
                break
            h.update(chunk)
            remaining -= len(chunk)
    return h.hexdigest()


def disk_md5(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def sfdisk_script(rng, disk_mib, table):
    """Random-but-valid initial layout."""
    lines = ["label: " + ("gpt" if table == "gpt" else "dos")]
    cursor = 1  # MiB
    free = disk_mib - cursor
    kind = rng.choice(["simple", "two", "tiny", "full", "holes",
                       "extended", "many"])
    idx = 0
    if kind == "full":
        lines.append("start=%dMiB, size=%dMiB, type=linux"
                     % (cursor, free))
        return "\n".join(lines) + "\n"
    if kind == "tiny":
        lines.append("start=%dMiB, size=1MiB, type=esp" % cursor)
        cursor += 2
        free -= 2
    if kind == "extended" and table == "mbr":
        ext_size = max(8, free // 2)
        lines.append("start=%dMiB, size=%dMiB, type=5" % (cursor, ext_size))
        inner = cursor + 1
        used = 1
        n = rng.randint(1, 3)
        for _ in range(n):
            sz = max(1, (ext_size - used - 1) // (n + 1))
            if inner + sz >= cursor + ext_size:
                break
            lines.append("start=%dMiB, size=%dMiB, type=83" % (inner, sz))
            inner += sz + 1
            used += sz + 1
        cursor += ext_size
        free -= ext_size
    count = {"simple": 1, "two": 2, "tiny": 1, "holes": 2,
             "many": rng.randint(3, 6)}.get(kind, 1)
    for i in range(count):
        if free < 4:
            break
        sz = max(2, free // (count - i))
        if kind == "holes" and i == 0:
            sz = max(2, free // 3)
            lines.append("start=%dMiB, size=%dMiB, type=linux"
                         % (cursor, sz))
            cursor += sz + rng.randint(1, 8)
            free = disk_mib - cursor
            continue
        gtype = "linux"
        if idx == 0 and table == "gpt" and rng.random() < 0.5:
            gtype = "esp"
        lines.append("start=%dMiB, size=%dMiB, type=%s"
                     % (cursor, min(sz, free), gtype))
        cursor += min(sz, free)
        free = disk_mib - cursor
        idx += 1
    return "\n".join(lines) + "\n"


def plan_line(kind, fields):
    out = ["[op]", "kind=" + kind]
    for key, value in fields:
        out.append("%s=%s" % (key, value))
    return "\n".join(out) + "\n"


def random_plan(rng, disk, parts, mode, fs_files):
    """mode: valid | invalid | mixed.

    On file disks only whole-disk ops and literal file targets pass
    validate (partition nodes are not real files), so "valid" plans stay
    within that subset; node-targeted ops appear in "mixed", where a
    clean validate rejection (2, disk untouched) is also acceptable.
    """
    ops = []
    touched_anything = False
    table = rng.choice(["gpt", "mbr"])
    if rng.random() < 0.35 or (mode == "valid" and rng.random() < 0.2):
        ops.append(plan_line("create_table",
                             [("id", "t0"), ("type", table)]))
        touched_anything = True
    for i in range(rng.randint(0, 3)):
        op = rng.choice(["create"] * 2 + ["format"]
                        + (["delete", "erase", "set_string", "set_flags",
                            "resize"] if mode == "mixed" else []))
        pid = "op%d" % i
        if op == "create":
            start = rng.randint(1, 200)
            size = rng.choice([-1, rng.randint(1, 64)])
            ops.append(plan_line("create", [
                ("id", pid), ("start_mib", start), ("size_mib", size),
                ("gpt_type", rng.choice(["linux", "esp", "linux_swap"])),
                ("role", rng.choice(["root", "none", "swap"]))]))
            touched_anything = True
        elif op == "format":
            target = rng.choice(fs_files) if fs_files else disk
            ops.append(plan_line("format", [
                ("id", pid), ("target_ref", target),
                ("filesystem", rng.choice(FILESYSTEMS)),
                ("label", "fz"), ("role", "root")]))
        else:
            if not parts:
                break
            p = rng.choice(parts)
            node = disk + p["node"].rsplit("img", 1)[-1]
            if op == "delete":
                ops.append(plan_line("delete",
                                     [("id", pid), ("target_ref", node)]))
                touched_anything = True
            elif op == "erase":
                ops.append(plan_line("erase",
                                     [("id", pid), ("target_ref", node)]))
            elif op == "set_string":
                ops.append(plan_line("set_string", [
                    ("id", pid), ("target_ref", node),
                    ("string_kind", rng.choice(["label", "gpt_name"])),
                    ("value", "fz")]))
            elif op == "set_flags":
                ops.append(plan_line("set_flags", [
                    ("id", pid), ("target_ref", node),
                    ("flag", rng.choice(["boot", "esp", "legacy_boot"]))]))
            elif op == "resize":
                ops.append(plan_line("resize", [
                    ("id", pid), ("target_ref", node),
                    ("filesystem", "ext4"),
                    ("new_size_mib", rng.randint(2, 128))]))
    if mode == "invalid":
        bad = rng.choice([
            ("format", [("id", "bad"), ("target_ref", disk),
                        ("filesystem", "notafs"), ("role", "root")]),
            ("create", [("id", "bad"), ("start_mib", "0"),
                        ("size_mib", "999999"), ("gpt_type", "linux")]),
            ("delete", [("id", "bad"), ("target_ref", "/nonexistent/x")]),
            ("create", [("id", "bad id!"), ("start_mib", "1"),
                        ("size_mib", "1"), ("gpt_type", "linux")]),
            ("set_flags", [("id", "bad"), ("target_ref", disk),
                           ("flag", "wobble")]),
        ])
        ops.append(plan_line(bad[0], bad[1]))
    header = "plan_version=1\ndisk=%s\ntable_type=%s\n\n" % (disk, table)
    return header + "\n".join(ops) + "\n", touched_anything


def make_stub_dir(work, fail_tools):
    stub = os.path.join(work, "stub-bin")
    os.makedirs(stub, exist_ok=True)
    for tool in fail_tools:
        path = os.path.join(stub, tool)
        with open(path, "w") as f:
            f.write("#!/bin/sh\necho injected failure >&2\nexit 1\n")
        os.chmod(path, 0o755)
    return stub


def check_case(base_seed, index, work):
    seed = base_seed * 1000003 + index
    rng = random.Random(seed)
    case_dir = os.path.join(work, "case-%d" % index)
    os.makedirs(case_dir, exist_ok=True)
    disk = os.path.join(case_dir, "disk.img")
    disk_mib = rng.choice([64, 128, 256, 512])
    with open(disk, "wb") as f:
        f.truncate(disk_mib * 1024 * 1024)
    table = rng.choice(["gpt", "mbr"])
    script = sfdisk_script(rng, disk_mib, table)
    res = run(["sfdisk", disk], input=script)
    if res.returncode != 0:
        return None  # generator produced something sfdisk refuses; skip

    parts = partitions(disk)
    before_parts = {(p["start"], p["size"]) for p in parts}
    regions = {p["node"]: (p["start"], p["size"]) for p in parts}
    before_regions = {n: region_md5(disk, s, sz)
                      for n, (s, sz) in regions.items()}

    flavor = rng.choice(["valid", "valid", "mixed", "invalid",
                         "corrupt", "stubbed"])
    fs_files = []
    for i in range(2):
        fp = os.path.join(case_dir, "fs%d.img" % i)
        with open(fp, "wb") as f:
            f.truncate(32 * 1024 * 1024)
        fs_files.append(fp)
    plan, touched = random_plan(
        rng, disk, parts,
        "invalid" if flavor == "invalid" else
        ("mixed" if flavor in ("mixed", "stubbed") else "valid"),
        fs_files)
    plan_path = os.path.join(case_dir, "plan.txt")
    with open(plan_path, "w") as f:
        f.write(plan)

    env = dict(os.environ)
    stub = None
    if flavor == "stubbed":
        stub = make_stub_dir(case_dir, ["mkfs.ext4", "mkfs.xfs",
                                        "mkfs.btrfs", "mkfs.fat"])
        env["PATH"] = stub + ":" + env["PATH"]
    if flavor == "corrupt":
        cut = rng.choice([4096, 512, 1024 * 1024])
        with open(disk, "r+b") as f:
            f.truncate(cut)

    before_disk = disk_md5(disk)
    proc = subprocess.run([APPLY, plan_path], capture_output=True,
                          text=True, env=env)
    rc = proc.returncode

    problems = []
    if rc < 0 or rc > 3:
        problems.append("exit code %d (crash or unexpected)" % rc)
    if flavor == "invalid":
        if rc != 2:
            problems.append("invalid plan should be rejected with 2, "
                            "got %d" % rc)
        if disk_md5(disk) != before_disk:
            problems.append("rejected plan modified the disk")
    elif flavor in ("valid", "mixed", "stubbed"):
        if rc not in (0, 2, 3):
            problems.append("plan apply returned %d" % rc)
        if rc == 2:
            # Validate refuses node refs that are not real files on a
            # file disk; the invariant is that nothing was written.
            if disk_md5(disk) != before_disk:
                problems.append("rejected plan modified the disk")
        else:
            # Untouched partitions keep their bytes and geometry.
            plan_nodes = set()
            for p in parts:
                node = p["node"]
                suffix = node.rsplit("img", 1)[-1]
                if node in plan or ("img" + suffix) in plan \
                        or suffix in plan:
                    plan_nodes.add(node)
            if not touched and "create_table" not in plan:
                after = partitions(disk)
                after_nodes = {p["node"]: (p["start"], p["size"])
                               for p in after}
                for node, geo in regions.items():
                    if node in plan_nodes:
                        continue
                    if node not in after_nodes:
                        if "delete" in plan:
                            continue
                        problems.append("partition %s vanished without a "
                                        "delete op" % node)
                        continue
                    if after_nodes[node] != geo:
                        if any(k in plan for k in ("resize", "move")):
                            continue
                        problems.append("partition %s geometry changed"
                                        % node)
                        continue
                    if after_nodes[node] == geo and \
                            region_md5(disk, geo[0], geo[1]) \
                            != before_regions[node]:
                        problems.append("untouched partition %s bytes "
                                        "changed" % node)
    elif flavor == "corrupt":
        if rc not in (0, 2, 3):
            problems.append("corrupt disk gave exit %d" % rc)

    return {
        "seed": seed,
        "index": index,
        "flavor": flavor,
        "rc": rc,
        "problems": problems,
        "dir": case_dir,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=20261013)
    ap.add_argument("--cases", type=int, default=60)
    ap.add_argument("--work", default=os.path.join(SIM, "fuzzwork"))
    ap.add_argument("--regressions",
                    default=os.path.join(SIM, "regressions.txt"))
    args = ap.parse_args()

    os.makedirs(args.work, exist_ok=True)
    if not os.path.exists(APPLY):
        print("run sim-install.sh first (builds the apply driver)",
              file=sys.stderr)
        return 2

    seeds = [(args.seed, i) for i in range(args.cases)]
    if os.path.exists(args.regressions):
        with open(args.regressions) as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#"):
                    reg = int(line)
                    seeds.append((reg // 1000003, reg % 1000003))

    failures = []
    ran = 0
    for base, index in seeds:
        result = check_case(base, index, args.work)
        if result is None:
            continue
        ran += 1
        if result["problems"]:
            failures.append(result)
            print("FAIL seed=%d flavor=%s rc=%d: %s" % (
                result["seed"], result["flavor"], result["rc"],
                "; ".join(result["problems"])), file=sys.stderr)
            print("     replay: fuzz.py --seed %d --cases %d "
                  "(case dir %s)" % (base, index + 1, result["dir"]),
                  file=sys.stderr)
        else:
            print("ok   seed=%d flavor=%s rc=%d" % (
                result["seed"], result["flavor"], result["rc"]))

    print("\n%d cases, %d failures" % (ran, len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
