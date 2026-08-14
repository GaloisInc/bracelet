#!/usr/bin/env python3
import os
import sys
from pathlib import Path

with open("/proc/self/coredump_filter", "w") as f:
    f.write("0xfff\n")

ptrace_scope = Path("/proc/sys/kernel/yama/ptrace_scope")
try:
    ptrace_scope.write_text("0\n")
except Exception as e:
    print(f"Failed to write ptrace_scope due to {e}")
print(f"ptrace_scope={repr(ptrace_scope.read_text())}")

if os.fork() == 0:
    cmd = "/opt/bracelet-llvm/bin/root_snapshot"
    os.execvp(cmd, [cmd])

os.execvp(sys.argv[1], sys.argv[1:])
