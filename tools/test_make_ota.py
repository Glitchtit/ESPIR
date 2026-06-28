#!/usr/bin/env python3
"""Self-test for make_ota.py. Run: python tools/test_make_ota.py"""
import os, sys, struct, json
sys.path.insert(0, os.path.dirname(__file__))
import make_ota as mo

def test_parse_define():
    text = "#define ESPIR_FW_VERSION 0x00010000u\n#define ESPIR_MANUF_CODE 0x1037\n"
    assert mo.parse_define(text, "ESPIR_FW_VERSION") == 0x00010000
    assert mo.parse_define(text, "ESPIR_MANUF_CODE") == 0x1037

def test_build_and_parse_header():
    fw = b"\xAA" * 100
    blob = mo.build_ota(fw, manuf=0x1037, image_type=0x0001,
                        file_version=0x00010000, header_string=b"ESPIR-MASTER")
    h = mo.parse_ota_header(blob)
    assert h["magic"] == 0x0BEEF11E
    assert h["manufacturer_code"] == 0x1037
    assert h["image_type"] == 0x0001
    assert h["file_version"] == 0x00010000
    assert h["header_length"] == 56
    assert h["total_size"] == 56 + 6 + len(fw)

def test_subelement_carries_firmware():
    fw = b"firmware-bytes-123"
    blob = mo.build_ota(fw, 0x1037, 0x0001, 0x00010000, b"ESPIR-MASTER")
    tag, length = struct.unpack_from("<HI", blob, 56)
    assert tag == 0x0000
    assert length == len(fw)
    assert blob[62:62 + length] == fw

def test_index_entry(tmp_path="/tmp/claude-make-ota-test.ota"):
    fw = b"\x01\x02\x03" * 50
    blob = mo.build_ota(fw, 0x1037, 0x0001, 0x00010000, b"ESPIR-MASTER")
    with open(tmp_path, "wb") as f:
        f.write(blob)
    entry = mo.index_entry(tmp_path, "https://example/espir-master.ota",
                           manuf=0x1037, image_type=0x0001,
                           file_version=0x00010000, model_id="ESPIR-MASTER")
    assert entry["fileVersion"] == 65536
    assert entry["manufacturerCode"] == 4151
    assert entry["imageType"] == 1
    assert entry["modelId"] == "ESPIR-MASTER"
    assert entry["fileSize"] == len(blob)
    assert len(entry["sha512"]) == 128  # hex of 64 bytes
    os.remove(tmp_path)

def test_upsert_index(tmp_path="/tmp/claude-make-ota-index.json"):
    if os.path.exists(tmp_path):
        os.remove(tmp_path)
    def entry(image_type, ver):
        return {"fileVersion": ver, "fileSize": 1, "manufacturerCode": 0x1037,
                "imageType": image_type, "sha512": "x", "url": "u",
                "modelId": "M" if image_type == 1 else "S"}
    # fresh index -> 1 entry
    idx = mo.upsert_index(tmp_path, entry(1, 65536))
    assert len(idx) == 1
    json.dump(idx, open(tmp_path, "w"))
    # different imageType -> added (2 entries), sorted by imageType
    idx = mo.upsert_index(tmp_path, entry(2, 65536))
    assert [e["imageType"] for e in idx] == [1, 2]
    json.dump(idx, open(tmp_path, "w"))
    # same imageType, newer version -> replaced in place, still 2 entries
    idx = mo.upsert_index(tmp_path, entry(1, 65537))
    assert len(idx) == 2
    master = [e for e in idx if e["imageType"] == 1][0]
    assert master["fileVersion"] == 65537
    os.remove(tmp_path)

if __name__ == "__main__":
    failures = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"ok   {name}")
            except Exception as e:
                failures += 1
                print(f"FAIL {name}: {e}")
    sys.exit(1 if failures else 0)
