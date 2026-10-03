#!/usr/bin/env python3
"""Real ELF differential, evidence, loader confinement and CI exit tests."""
import json
from pathlib import Path
import subprocess
import struct
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()


def invoke(*args, code=0):
    result = subprocess.run([str(binary), *map(str, args)], text=True, capture_output=True)
    assert result.returncode == code, (result.args, result.returncode, result.stdout, result.stderr)
    return json.loads(result.stdout)


with tempfile.TemporaryDirectory(prefix="abilens-evidence-") as tmp:
    root = Path(tmp)
    source = root / "fixture.cpp"
    source.write_text('struct Layout { int first; long second; };\nextern "C" { Layout exported; int entry() { return 1; } }\n')
    left = root / "left.so"
    subprocess.run(["c++", "-g", "-fPIC", "-shared", str(source), "-o", str(left), "-Wl,-soname,libfixture.so", "-Wl,-rpath,/first::/second:/first"], check=True)
    a = invoke("inspect", "--json", left)
    assert a["schema"] == "abilens.report/v2"
    assert a["evidence"]["soname"] == "libfixture.so"
    assert a["evidence"]["build_id"]
    assert a["dependencies"]["runpath"] == ["/first", "", "/second", "/first"]
    symbols = {s["identity"]: s for s in a["evidence"]["symbol_evidence"]}
    assert symbols["exported"]["type"] == 1 and symbols["exported"]["size"] == 16
    assert symbols["entry"]["type"] == 2 and symbols["entry"]["binding"] == 1
    readelf = subprocess.check_output(["readelf", "-Ws", str(left)], text=True)
    assert "exported" in readelf and "entry" in readelf
    saved = root / "report.json"
    saved.write_text(json.dumps(a))
    assert invoke("diff", "--json", saved, left)["compatibility"] == "compatible"
    legacy = dict(a)
    legacy["schema"] = "abilens.report/v1"
    del legacy["evidence"]
    saved.write_text(json.dumps(legacy))
    assert invoke("diff", "--json", "--fail-on", "unknown", saved, left, code=2)["compatibility"] == "unknown"
    source.write_text('extern "C" { int exported; }\n')
    right = root / "right.so"
    subprocess.run(["c++", "-g", "-fPIC", "-shared", str(source), "-o", str(right), "-Wl,-soname,libfixture.so"], check=True)
    diff = invoke("diff", "--json", "--fail-on", "incompatible", left, right, code=2)
    assert diff["compatibility"] == "incompatible"
    assert "entry" in diff["symbols"]["removed"]
    assert any("exported: size" in change for change in diff["symbol_changes"])
    assert invoke("diff", "--json", "--fail-on", "never", left, right)["changed"]
    dwarf = invoke("inspect", "--json", "--dwarf", left)
    if dwarf["evidence"]["dwarf_status"] != "unavailable":
        assert dwarf["evidence"]["dwarf_status"] == "complete", dwarf
        assert any("Layout|size=16" in layout and "second:" in layout for layout in dwarf["evidence"]["type_layouts"])
        saved.write_text(json.dumps(dwarf))
        assert invoke("diff", "--json", "--dwarf", saved, left)["compatibility"] == "compatible"
    if dwarf["evidence"]["dwarf_status"] == "complete":
        layouts = []
        for index, field in enumerate(("int", "long")):
            source.write_text(f'struct Public {{ {field} member; }}; extern "C" int access(Public* p) {{ return p->member; }}\n')
            path = root / f"layout{index}.so"
            subprocess.run(["c++", "-g", "-shared", "-fPIC", str(source), "-o", str(path)], check=True)
            layouts.append(path)
        layout_diff = invoke("diff", "--json", "--dwarf", *layouts)
        assert layout_diff["types"]["removed"] and layout_diff["types"]["added"]
        assert layout_diff["compatibility"] == "unknown"
        source.write_text('namespace first { struct Same { int x; }; } namespace second { struct Same { int x; }; } first::Same one; second::Same two;\n')
        named = root / "namespaced.so"
        subprocess.run(["c++", "-g", "-shared", "-fPIC", str(source), "-o", str(named)], check=True)
        names = invoke("inspect", "--dwarf", "--json", named)["evidence"]["type_layouts"]
        assert any(name.startswith("first::Same|") for name in names)
        assert any(name.startswith("second::Same|") for name in names)
    source.write_text('extern "C" { int exported; }\n')
    untyped = []
    for size in (4, 8):
        assembly = root / f"untyped{size}.s"
        assembly.write_text(f".data\n.globl exported\n.size exported,{size}\nexported:\n.zero {size}\n")
        path = root / f"untyped{size}.so"
        subprocess.run(["cc", "-nostdlib", "-shared", str(assembly), "-o", str(path)], check=True)
        untyped.append(path)
    untyped_diff = invoke("diff", "--json", *untyped)
    assert untyped_diff["changed"] and untyped_diff["compatibility"] == "unknown"
    if dwarf["evidence"]["dwarf_status"] == "complete":
        type_units = []
        for size in (1, 2):
            source.write_text(f'struct TypeUnit {{ int data[{size}]; }}; extern "C" int api(TypeUnit* p) {{ return p->data[0]; }}\n')
            path = root / f"type-unit{size}.so"
            subprocess.run(["c++", "-gdwarf-4", "-fdebug-types-section", "-shared", "-fPIC", str(source), "-o", str(path)], check=True)
            type_units.append(path)
        unit_diff = invoke("diff", "--json", "--dwarf", *type_units)
        assert unit_diff["types"]["removed"] and unit_diff["types"]["added"]
        assert unit_diff["compatibility"] == "unknown"
    if dwarf["evidence"]["dwarf_status"] == "complete":
        bitfields = []
        for padding in (1, 2):
            source.write_text(f'struct Bits {{ unsigned :{padding}; unsigned value:2; }}; extern "C" int api(Bits* p) {{ return p->value; }}\n')
            path = root / f"bits{padding}.so"
            subprocess.run(["c++", "-gdwarf-4", "-shared", "-fPIC", str(source), "-o", str(path)], check=True)
            bitfields.append(path)
        bit_diff = invoke("diff", "--json", "--dwarf", *bitfields)
        assert bit_diff["types"]["removed"] and bit_diff["types"]["added"]
        assert bit_diff["compatibility"] == "unknown"
    source.write_text('extern "C" { int exported; }\n')
    # ELF32 validates the actual st_shndx position (14, not 12).
    elf32 = root / "elf32.so"
    subprocess.run(["c++", "-m32", "-nostdlib", "-shared", "-fPIC", str(source), "-o", str(elf32)], check=True)
    assert "exported" in invoke("inspect", "--json", elf32)["symbols"]
    # Kernel-confined target lookup honors rootfs-absolute symlinks.
    sysroot = root / "sysroot"
    (sysroot / "lib").mkdir(parents=True)
    consumer_src = root / "consumer.cpp"
    consumer_src.write_text('extern "C" int entry(); int use() { return entry(); }\n')
    consumer = root / "consumer.so"
    subprocess.run(["c++", "-shared", "-fPIC", str(consumer_src), str(left), "-o", str(consumer)], check=True)
    (sysroot / "lib/real.so").write_bytes(left.read_bytes())
    (sysroot / "lib/libfixture.so").symlink_to("/lib/real.so")
    inspected = invoke("inspect", "--json", "--sysroot", sysroot, consumer)
    resolution = next(item for item in inspected["evidence"]["resolutions"] if item["needed"] == "libfixture.so")
    assert resolution["status"] == "candidate", resolution
    big_header = b"\x7fELF\x02\x02\x01" + bytes(9) + struct.pack(
        ">HHIQQQIHHHHHH", 3, 62, 1, 0, 0, 0, 0, 64, 56, 0, 64, 0, 0
    )
    (sysroot / "lib/real.so").write_bytes(big_header)
    inspected = invoke("inspect", "--json", "--sysroot", sysroot, consumer)
    resolution = next(item for item in inspected["evidence"]["resolutions"] if item["needed"] == "libfixture.so")
    assert resolution["status"] == "unresolved"
    (sysroot / "lib/libfixture.so").unlink()
    (sysroot / "lib/libfixture.so").symlink_to(left)
    inspected = invoke("inspect", "--json", "--sysroot", sysroot, consumer)
    resolution = next(item for item in inspected["evidence"]["resolutions"] if item["needed"] == "libfixture.so")
    assert resolution["status"] == "unresolved"
print("test_evidence: PASS")
