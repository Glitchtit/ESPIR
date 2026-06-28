#!/usr/bin/env python3
"""
Wrap an ESP-IDF app binary in a Zigbee OTA Upgrade file and emit a Z2M index.

Usage:
  python tools/make_ota.py \
      --bin master/build/espir_master.bin \
      --proto components/espir_zcl/include/espir_proto.h \
      --url-base https://raw.githubusercontent.com/Glitchtit/ESPIR/<branch>/z2m/ota \
      --out-dir z2m/ota --model ESPIR-MASTER

Reads ESPIR_FW_VERSION / ESPIR_OTA_IMAGE_TYPE / ESPIR_MANUF_CODE from the proto header
so the .ota header always matches the firmware. Writes <out-dir>/espir-master.ota and
<out-dir>/index.json.
"""
import argparse, hashlib, json, os, re, struct

OTA_MAGIC      = 0x0BEEF11E
HEADER_VERSION = 0x0100
FIELD_CONTROL  = 0x0000
STACK_VERSION  = 0x0002          # Zigbee Pro
HEADER_LEN     = 56              # no optional header fields
TAG_UPGRADE    = 0x0000          # "Upgrade Image" sub-element
HEADER_FMT     = "<IHHHHHIH32sI" # magic, hdrver, hdrlen, fieldctl, manuf, imgtype,
                                 # fileversion, stackver, string[32], totalsize

def parse_define(text, name):
    m = re.search(r"#define\s+" + re.escape(name) + r"\s+(0x[0-9A-Fa-f]+|\d+)", text)
    if not m:
        raise ValueError(f"{name} not found in header")
    return int(m.group(1), 0)

def build_ota(firmware, manuf, image_type, file_version, header_string=b"ESPIR-MASTER"):
    total = HEADER_LEN + 6 + len(firmware)   # 6 = sub-element tag(2)+length(4)
    header = struct.pack(HEADER_FMT, OTA_MAGIC, HEADER_VERSION, HEADER_LEN, FIELD_CONTROL,
                         manuf, image_type, file_version, STACK_VERSION,
                         header_string.ljust(32, b"\x00")[:32], total)
    sub = struct.pack("<HI", TAG_UPGRADE, len(firmware)) + firmware
    return header + sub

def parse_ota_header(blob):
    (magic, hdrver, hdrlen, fieldctl, manuf, imgtype,
     fileversion, stackver, string, total) = struct.unpack_from(HEADER_FMT, blob, 0)
    return {
        "magic": magic, "header_version": hdrver, "header_length": hdrlen,
        "field_control": fieldctl, "manufacturer_code": manuf, "image_type": imgtype,
        "file_version": fileversion, "stack_version": stackver,
        "header_string": string.rstrip(b"\x00").decode(), "total_size": total,
    }

def index_entry(ota_path, url, manuf, image_type, file_version, model_id):
    data = open(ota_path, "rb").read()
    return {
        "fileVersion": file_version,
        "fileSize": len(data),
        "manufacturerCode": manuf,
        "imageType": image_type,
        "sha512": hashlib.sha512(data).hexdigest(),
        "url": url,
        "modelId": model_id,
    }

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", required=True)
    ap.add_argument("--proto", required=True)
    ap.add_argument("--url-base", required=True, help="raw URL dir holding the .ota file")
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--model", default="ESPIR-MASTER")
    args = ap.parse_args()

    proto = open(args.proto).read()
    manuf      = parse_define(proto, "ESPIR_MANUF_CODE")
    image_type = parse_define(proto, "ESPIR_OTA_IMAGE_TYPE")
    file_ver   = parse_define(proto, "ESPIR_FW_VERSION")

    firmware = open(args.bin, "rb").read()
    blob = build_ota(firmware, manuf, image_type, file_ver, args.model.encode())

    # round-trip self-check
    h = parse_ota_header(blob)
    assert h["magic"] == OTA_MAGIC and h["manufacturer_code"] == manuf
    assert h["image_type"] == image_type and h["file_version"] == file_ver
    assert h["total_size"] == len(blob)

    os.makedirs(args.out_dir, exist_ok=True)
    ota_name = "espir-master.ota"
    ota_path = os.path.join(args.out_dir, ota_name)
    with open(ota_path, "wb") as f:
        f.write(blob)

    url = args.url_base.rstrip("/") + "/" + ota_name
    entry = index_entry(ota_path, url, manuf, image_type, file_ver, args.model)
    with open(os.path.join(args.out_dir, "index.json"), "w") as f:
        json.dump([entry], f, indent=2)
        f.write("\n")

    print(f"wrote {ota_path} ({len(blob)} bytes), fileVersion=0x{file_ver:08x}")
    print(f"index url: {url}")

if __name__ == "__main__":
    main()
