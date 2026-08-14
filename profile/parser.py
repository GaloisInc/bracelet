import json
import csv
import argparse
from pathlib import Path
import json


def file_to_test_time(fl: Path) -> dict[str, float]:
    txt = fl.read_text()
    its = json.loads(txt)
    tsts = its["tests"]
    res = {}
    for tst in tsts:
        res[tst["name"]] = tst["metrics"]["compile_time"]
    return res


def main():
    prsrs = argparse.ArgumentParser()
    prsrs.add_argument("file1", type=Path)
    prsrs.add_argument("file2", type=Path)
    prsrs.add_argument("--name1", required=True)
    prsrs.add_argument("--name2", required=True)
    prsrs.add_argument("-o", required=True, type=Path)
    args = prsrs.parse_args()

    t1 = file_to_test_time(args.file1)
    t2 = file_to_test_time(args.file2)
    output: Path = args.o
    with output.open("w", newline="") as csvfile:
        wtr = csv.writer(csvfile, delimiter=",")
        wtr.writerow(["Testcase", args.name1, args.name2, "Percentage change from n2"])
        # assume same keys
        for k in t1.keys():
            n1 = t1[k]
            n2 = t2[k]
            diff = ((n1 - n2) / n2) * 100
            wtr.writerow([k, n1, n2, diff])


if __name__ == "__main__":
    main()
