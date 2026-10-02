"""Source contract, not a hardware test: all core replacement paths must stop Groovy first."""
import pathlib
import re
import sys

source = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8-sig")
for name, declaration, ret in (
    ("fpga_load_rbf", "int", "-1"),
    ("reboot", "void", ""),
    ("app_restart", "void", ""),
):
    start = re.search(rf"\b{declaration}\s+{name}\([^)]*\)\s*\{{", source)
    assert start, name
    depth, end = 1, start.end()
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    body = source[start.end():end - 1]
    gate = re.search(rf"if\s*\(\s*!groovy_stop\(\)\s*\)\s*return\s*{ret}\s*;", body)
    assert gate, f"{name}: missing checked cleanup hook"
    operations = list(re.finditer(r"\b(fpga_core_reset|do_bridge|socfpga_load|execl|writel)\s*\(", body))
    assert operations and all(gate.end() < operation.start() for operation in operations), name
print("PASS: checked Groovy cleanup before FPGA load/reset and exec in all three common entry points")
