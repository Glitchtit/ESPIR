#!/usr/bin/env python3
"""
Wrap an ESP-IDF app binary in a Zigbee OTA Upgrade file and upsert it into a Z2M index.

Usage (one invocation per product binary):
  python tools/make_ota.py \
      --bin master/build/espir_master.bin \
      --proto components/espir_zcl/include/espir_proto.h \
      --url-base https://raw.githubusercontent.com/Glitchtit/ESPIR/<branch>/z2m/ota \
      --out-dir z2m/ota --model ESPIR-MASTER \
      --image-type 1 --out-name espir-master.ota

Reads ESPIR_FW_VERSION / ESPIR_MANUF_CODE from the proto header so the .ota header always
matches the firmware; the image type is passed explicitly (master=1, slave=2, slave-pcb=3)
because the three product binaries share the manufacturer code. Writes <out-dir>/<out-name>
and upserts a matching entry into <out-dir>/index.json (keyed by manufacturerCode+imageType),
so multiple products coexist in one index.
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

def upsert_index(index_path, entry):
    """Load index.json (or []), replace any entry with the same
    (manufacturerCode, imageType), add this one, return the sorted list."""
    index = []
    if os.path.exists(index_path):
        with open(index_path) as f:
            index = json.load(f)
    key = (entry["manufacturerCode"], entry["imageType"])
    index = [e for e in index
             if (e["manufacturerCode"], e["imageType"]) != key]
    index.append(entry)
    index.sort(key=lambda e: (e["manufacturerCode"], e["imageType"]))
    return index

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", required=True)
    ap.add_argument("--proto", required=True)
    ap.add_argument("--url-base", required=True, help="raw URL dir holding the .ota file")
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--model", required=True, help="modelId, e.g. ESPIR-MASTER / ESPIR-SLAVE")
    ap.add_argument("--image-type", type=int, required=True,
                    help="OTA image type (master=1, slave=2, slave-pcb=3)")
    ap.add_argument("--out-name", required=True, help="output .ota filename, e.g. espir-master.ota")
    args = ap.parse_args()

    proto = open(args.proto).read()
    manuf    = parse_define(proto, "ESPIR_MANUF_CODE")
    file_ver = parse_define(proto, "ESPIR_FW_VERSION")
    image_type = args.image_type

    firmware = open(args.bin, "rb").read()
    blob = build_ota(firmware, manuf, image_type, file_ver, args.model.encode())

    # round-trip self-check
    h = parse_ota_header(blob)
    assert h["magic"] == OTA_MAGIC and h["manufacturer_code"] == manuf
    assert h["image_type"] == image_type and h["file_version"] == file_ver
    assert h["total_size"] == len(blob)

    os.makedirs(args.out_dir, exist_ok=True)
    ota_path = os.path.join(args.out_dir, args.out_name)
    with open(ota_path, "wb") as f:
        f.write(blob)

    url = args.url_base.rstrip("/") + "/" + args.out_name
    entry = index_entry(ota_path, url, manuf, image_type, file_ver, args.model)
    index_path = os.path.join(args.out_dir, "index.json")
    index = upsert_index(index_path, entry)
    with open(index_path, "w") as f:
        json.dump(index, f, indent=2)
        f.write("\n")

    print(f"wrote {ota_path} ({len(blob)} bytes), imageType={image_type}, "
          f"fileVersion=0x{file_ver:08x}")
    print(f"index now has {len(index)} entries; url: {url}")

if __name__ == "__main__":
    main()
