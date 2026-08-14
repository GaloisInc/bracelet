#!/usr/bin/env python3
import argparse
from pathlib import Path
import glob
from dataclasses import dataclass
import json
import sys
from typing import Optional, Callable
from hashlib import sha256
import base64
from uuid import uuid4
import os

PORT_SPDX_ID = "SPDXRef-port"

@dataclass
class PortDef:
    name:str
    version: str

HEADER_FILE_SUFFIXES = [".h", ".hpp"]



def find_files_from_path_with_prop(prop: Callable[[Path], bool], pth: Path):
    for (pth, _, filenames) in os.walk(pth):
        for fl in filenames:
            flPth = Path(pth) / fl
            if prop(flPth):
                yield flPth
    return None


def sbom_info_for_dir(target: Path) -> dict[str, dict[str,str]]:
    headers: set[str] = set()
    glob_path = target
    for hdr in find_files_from_path_with_prop(lambda p: any([p.name.endswith(suffix) for suffix in HEADER_FILE_SUFFIXES]), glob_path):
        headers.add(hdr)

    package_specs: set[str] = set()
    def is_spdx(p: Path):
        has_share = "share" in p.parts
        return has_share and p.name == "vcpkg.spdx.json"

    for f in find_files_from_path_with_prop(is_spdx, glob_path):
        package_specs.add(f)
    package_defs: list[PortDef] = []
    for s in package_specs:
        with open(s, "r") as f:
            spdx = json.load(f)
            try: 
                for pkg in spdx["packages"]:
                    if pkg["SPDXID"] == PORT_SPDX_ID:
                        nm = pkg["name"]
                        vinfo = pkg["versionInfo"]
                        package_defs.append(PortDef(nm, vinfo))
            except Exception as E:
                print(f"Error: {E}", file=sys.stderr)
                pass
    


    name_to_portdef = dict([(pdef.name, pdef) for pdef in package_defs])
    sbom_info: dict[str,dict[str,str]] = dict()
    

    def include_dir(pth: Path):
        try:
            return pth.parts[pth.parts.index("include") + 1]
        except:
            return None

    def get_port_name_of_header(header: Path) -> PortDef:
         for hsuffix in HEADER_FILE_SUFFIXES:
            if header.name.endswith(hsuffix):
                trimmed = header.name[0:len(header.name)-len(hsuffix)]
                if trimmed in name_to_portdef:
                    return name_to_portdef[trimmed]

                removed_underscore = header.name.split("_")[0]
                if removed_underscore in name_to_portdef:
                    return name_to_portdef[removed_underscore]
                    
         idir = include_dir(header)
         if idir is not None and idir in name_to_portdef:
             return name_to_portdef[idir]
         return None

    for hfile in headers:
        hfile_pth = Path(hfile).absolute()
        port = get_port_name_of_header(hfile_pth)
        if port is not None:
            sbom_info[str(hfile_pth)] = {"port-name": port.name, "port-version": port.version}

    return sbom_info


def check_flag_for_include(arr: list[str], curr_item: str, idx:int , target_flag) -> Optional[str]:
    if curr_item == target_flag and idx + 1 < len(arr):
        return arr[idx + 1]
    elif curr_item.startswith(target_flag):
        return curr_item[len(target_flag):]
    return None


def main():
    target_dirs = set()
    # TODO this list is not complete and also over approx since you cannot write -I <blah>(i think)
    target_flags = ["-I", "--include-directory", "-isystem","--isystem", "--cxx-isystem", "-isystem", "-cxx-isystem"]
    for (idx, item) in enumerate(sys.argv):
        for flg in target_flags:
            res = check_flag_for_include(sys.argv, item, idx, flg)
            if res is not None:
                target_dirs.add(res)

    sbom_info = dict()
    for target_dir in target_dirs:
        pth = Path(target_dir).absolute()
        if len(pth.parts) <= 0:
            continue
        # trim include so maybe we can find a ./shared for a vcpkg target
        if pth.parts[-1] == "include":
            pth = pth.parent
        sbom_info.update(sbom_info_for_dir(pth)) 
    sbom_json = json.dumps(sbom_info, indent=4)
    h = base64.urlsafe_b64encode(sha256(sbom_json.encode("utf-8")).digest()).decode("ascii")[0:32]
    dst = f"/tmp/sbom-{h}.json"
    path = f"{dst}-{uuid4()}"
    with open(path, "w") as f:
        f.write(sbom_json)
    os.replace(path, dst)
    print(dst)


if __name__ == "__main__":
    main()
