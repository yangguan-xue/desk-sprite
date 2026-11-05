"""Validate actual NVS entry bytes, not a second implementation of generation."""
import importlib.util
import os
from pathlib import Path
import struct
import unittest
import zlib

spec = importlib.util.spec_from_file_location(
    "provision_nvs", Path(__file__).parents[2] / "tools/provision_nvs.py")
provision = importlib.util.module_from_spec(spec)
spec.loader.exec_module(provision)


class ProvisionTests(unittest.TestCase):
    def test_unicode_length_and_crc(self):
        config = dict(wifi_ssid="测试网络", wifi_password="test-pass",
                      mqtt_username="dev_test", mqtt_password="mqtt-test")
        data = provision.build_nvs(config, Path(os.environ["IDF_PATH"]))
        self.assertEqual(len(data), 0x5000)
        header = data[96:128]  # namespace entry, then wifi_ssid
        size = struct.unpack_from("<H", header, 24)[0]
        value = data[128:128 + size]
        self.assertEqual(value, config["wifi_ssid"].encode() + b"\0")
        self.assertEqual(struct.unpack_from("<I", header, 28)[0],
                         zlib.crc32(value, 0xffffffff))

    def test_invalid_values(self):
        base = dict(wifi_ssid="test", wifi_password="test-pass",
                    mqtt_username="dev_test", mqtt_password="mqtt-test")
        for value in ("测" * 11, "", "a\0b"):
            with self.assertRaises(ValueError):
                provision.build_nvs({**base, "wifi_ssid": value}, Path(os.environ["IDF_PATH"]))


if __name__ == "__main__":
    unittest.main()
