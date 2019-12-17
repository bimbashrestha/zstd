import argparse
import glob
import os
import pickle as pk
import urllib.request
import json


GITHUB_API_PR_URL = "https://api.github.com/repos/bimbashrestha/zstd/pulls?state=open"
GIRHUB_URL_TEMPLATE = "https://github.com/{}/zstd"
MASTER_BUILD = {"user": "facebook", "branch": "dev", "hash": None}
PREVIOUS_PRS_FILENAME = "prev_prs.pk"

# Not sure what the threshold for triggering alarms should be
# 1% regression sounds like a little too sensitive but the desktop
# that I'm running it on is pretty stable so I think this is fine
CSPEED_REGRESSION_TOLERANCE = 0.01
DSPEED_REGRESSION_TOLERANCE = 0.01
N_BENCHMARK_ITERATIONS = 5


def get_open_prs(prev_state=True):
    prev_prs = None
    if os.path.exists(PREVIOUS_PRS_FILENAME):
        with open(PREVIOUS_PRS_FILENAME, "rb") as f:
            prev_prs = pk.load(f)
    data = json.loads(urllib.request.urlopen(GITHUB_API_PR_URL).read().decode("utf-8"))
    prs = {
        d["url"]: {
            "user": d["user"]["login"],
            "branch": d["head"]["ref"],
            "hash": d["head"]["sha"].strip(),
        }
        for d in data
    }
    with open(PREVIOUS_PRS_FILENAME, "wb") as f:
        pk.dump(prs, f)
    if not prev_state or prev_prs == None:
        return list(prs.values())
    return [pr for url, pr in prs.items() if url not in prev_prs or prev_prs[url] != pr]


def get_latest_hash():
    os.system("git log -1 &> tmp")
    with open("tmp", "r") as f:
        tmp = f.read()
        sha = tmp.split("\n")[0].split(" ")[1]
    os.system("rm -rf tmp")
    return sha.strip()

def get_parent_hash(sha, idx):
    os.system("git show {}^{} &> tmp".format(sha, idx))
    with open("tmp", "r") as f:
        tmp = f.read()
        sha = tmp.split("\n")[0].split(" ")[1]
    os.system("rm -rf tmp")
    return sha.strip()

def get_build_for_latest_hash():
    latest_hash = get_latest_hash()
    hashes = [latest_hash, get_parent_hash(latest_hash, 1), get_parent_hash(latest_hash, 2)]
    print(hashes)
    builds = get_open_prs(False)
    for b in builds:
        if b["hash"] in hashes:
            return [b]
    return []


def clone_and_build(build):
    github_url = "https://github.com/{}/zstd".format(build["user"])
    os.system(
        """
        rm -rf zstd-{sha} &&
        git clone {github_url} zstd-{sha} &&
        cd zstd-{sha} &&
        {checkout_command}
        make &&
        cd ../
    """.format(
            github_url=github_url,
            sha=build["hash"],
            checkout_command="git checkout {} &&".format(build["hash"])
            if build["hash"] != None
            else "",
        )
    )
    return "zstd-{sha}/zstd".format(sha=build["hash"])


def bench(executable, level, filename):
    os.system("{} -qb{} {} &> tmp".format(executable, level, filename))
    with open("tmp", "r") as f:
        output = f.read().split(" ")
        idx = [i for i, d in enumerate(output) if d == "MB/s"]
        cspeed, dspeed = float(output[idx[0] - 1]), float(output[idx[1] - 1])
    os.system("rm -rf tmp")
    return [cspeed, dspeed]


def bench_n(executable, level, filename, n=N_BENCHMARK_ITERATIONS):
    speeds_arr = [bench(executable, level, filename) for _ in range(n)]
    speeds = (max(b[0] for b in speeds_arr), max(b[1] for b in speeds_arr))
    print(
        "Bench (executable={} level={} filename={}, iterations={}):\n\t[cspeed: {} MB/s, dspeed: {} MB/s]".format(
            os.path.basename(executable),
            level,
            os.path.basename(filename),
            n,
            speeds[0],
            speeds[1],
        )
    )
    return speeds


def bench_cycle(build, filenames, levels):
    executable = clone_and_build(build)
    return [[bench_n(executable, l, f) for f in filenames] for l in levels]


def compare_versions(baseline_build, test_build, filenames, levels):
    old = bench_cycle(baseline_build, filenames, levels)
    new = bench_cycle(test_build, filenames, levels)
    regressions = []
    for j, level in enumerate(levels):
        for k, filename in enumerate(filenames):
            old_cspeed, old_dspeed = old[j][k]
            new_cspeed, new_dspeed = new[j][k]
            cspeed_reg = (old_cspeed - new_cspeed) / old_cspeed
            dspeed_reg = (old_dspeed - new_dspeed) / old_dspeed
            baseline_label = "{}:{} ({})".format(
                baseline_build["user"], baseline_build["branch"], baseline_build["hash"]
            )
            test_label = "{}:{} ({})".format(
                test_build["user"], test_build["branch"], test_build["hash"]
            )
            if cspeed_reg > CSPEED_REGRESSION_TOLERANCE:
                regressions.append(
                    "[COMPRESSION REGRESSION] (level={} filename={})\n\t{} -> {}\n\t{} -> {} ({:0.2f}%)".format(
                        level,
                        filename,
                        baseline_label,
                        test_label,
                        old_cspeed,
                        new_cspeed,
                        cspeed_reg * 100.0,
                    )
                )
            if dspeed_reg > DSPEED_REGRESSION_TOLERANCE:
                regressions.append(
                    "[DECOMPRESSION REGRESSION] (level={} filename={})\n\t{} -> {}\n\t{} -> {} ({:0.2f}%)".format(
                        level,
                        filename,
                        baseline_label,
                        test_label,
                        old_dspeed,
                        new_dspeed,
                        dspeed_reg * 100.0,
                    )
                )
    return regressions


def benchmark(filenames, levels, emails, builds=None):
    builds = get_open_prs() if builds == None else builds
    for test_build in builds:
        regressions = compare_versions(MASTER_BUILD, test_build, filenames, levels)
        body = "\n".join(regressions)
        if len(regressions) > 0:
            if emails != None:
                os.system(
                    """
                    echo "{}" | mutt -s "[zstd regression] caused by new pr" {}
                """.format(
                        body, emails
                    )
                )
            print(body)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "directory", help="Directory with files to benchmark", default="fuzz"
    )
    parser.add_argument(
        "levels", help="Which levels to test eg ('1,2,3')", default="1,2,3"
    )
    parser.add_argument(
        "mode", help="0 for pr run, 1 for benchmark machine run", default="0"
    )
    parser.add_argument(
        "emails",
        help="Email addresses of people who will be alerted upon regression",
        default=None,
    )
    args = parser.parse_args()
    filenames = glob.glob("{}/**".format(args.directory))
    emails = args.emails
    mode = args.mode
    levels = [int(l) for l in args.levels.split(",")]
    builds = get_build_for_latest_hash() if mode == "0" else None
    benchmark(filenames, levels, emails, builds)
