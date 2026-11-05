#!/usr/bin/env python3
"""Generate private sprite NVS with ESP-IDF, preserving UTF-8 byte lengths."""
import argparse
import importlib.util
import io
import json
import os
from pathlib import Path

LIMITS = {"wifi_ssid": 32, "wifi_password": 64,
          "mqtt_username": 63, "mqtt_password": 127}


def build_nvs(config, idf_path):
    encoded = {}
    for key, limit in LIMITS.items():
        value = config.get(key)
        if not isinstance(value, str) or not value or "\0" in value:
            raise ValueError(f"Missing or invalid {key}")
        raw = value.encode("utf-8")
        if len(raw) > limit:
            raise ValueError(f"Value exceeds byte capacity: {key}")
        encoded[key] = raw + b"\0"

    generator = idf_path / "components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py"
    spec = importlib.util.spec_from_file_location("sprite_idf_nvs_generator", generator)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    result = io.BytesIO()
    with module.nvs_open(result, 0x4000, module.Page.VERSION2) as nvs:
        nvs.write_namespace("sprite")
        # All four bounded values fit in one page. Bypass write_entry's str
        # conversion: IDF 5.1.2 computes len(str) before UTF-8 encoding there.
        for key, raw in encoded.items():
            nvs.cur_page.write_varlen_data(key, raw, "string", nvs.namespace_idx, nvs)
    image = result.getvalue()
    if len(image) != 0x5000:
        raise ValueError("Unexpected NVS partition size")
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--idf-path", type=Path,
                        default=Path(os.environ["IDF_PATH"]) if "IDF_PATH" in os.environ else None)
    args = parser.parse_args()
    if args.idf_path is None:
        parser.error("Set IDF_PATH or provide --idf-path")
    image = build_nvs(json.loads(args.config.read_text()), args.idf_path)
    # Never expose credentials or silently overwrite an unrelated output file.
    descriptor = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "wb") as output:
        output.write(image)
    print("Private NVS generated (0x5000 bytes)")


if __name__ == "__main__":
    main()
