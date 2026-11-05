"""Reject unsafe or incompatible assets before they reach the embedded parser."""
import struct
import unittest

from tools.video_probe.prepare_asset import validate_avi


def chunk(kind, data):
    return kind + struct.pack('<I', len(data)) + data + b'\0' * (len(data) % 2)


def fixture(codec=b'MJPG', channels=2, payload=None):
    video_header = struct.pack('<4s4sIHHIIIIIIIIhhhh', b'vids', codec, 0, 0, 0,
                               0, 1, 12, 0, 96, 65536, 0, 0, 0, 0, 240, 136)
    video_format = struct.pack('<IiiHH4sIIiiII', 40, 240, 136, 1, 24, codec,
                               240 * 136 * 3, 0, 0, 0, 0, 0)[:40]
    audio_header = struct.pack('<4s4sIHHIIIIIIIIhhhh', b'auds', b'\0' * 4, 0, 0, 0,
                               0, 4, 64000, 0, 128000, 4096, 0, 4, 0, 0, 0, 0)
    audio_format = struct.pack('<HHIIHH', 1, channels, 16000, 64000, 4, 16)
    streams = b''.join(chunk(b'LIST', b'strl' + chunk(b'strh', h) + chunk(b'strf', f))
                       for h, f in [(video_header, video_format), (audio_header, audio_format)])
    avih = struct.pack('<14I', 83333, 0, 0, 0, 96, 0, 2, 65536, 240, 136, 0, 0, 0, 0)
    # A real baseline JPEG is required, not just SOI/EOI marker bytes.
    if payload is None:
        import io
        from PIL import Image
        buf = io.BytesIO()
        Image.new('RGB', (240, 136), 'red').save(buf, format='JPEG')
        payload = buf.getvalue()
    movi = b''.join(chunk(b'00dc', payload) for _ in range(96))
    movi += b''.join(chunk(b'01wb', b'\0' * 4096) for _ in range(125))
    body = b'AVI ' + chunk(b'LIST', b'hdrl' + chunk(b'avih', avih) + streams)
    body += chunk(b'LIST', b'movi' + movi)
    return chunk(b'RIFF', body)


class VideoAssetTest(unittest.TestCase):
    def test_accepts_expected_video_and_pcm(self):
        result = validate_avi(fixture())
        self.assertEqual(result['video_frames'], 96)
        self.assertEqual(result['audio_bytes'], 512000)

    def test_rejects_truncated_container(self):
        with self.assertRaises(ValueError):
            validate_avi(fixture()[:-1])

    def test_rejects_unsupported_codec(self):
        with self.assertRaises(ValueError):
            validate_avi(fixture(codec=b'H264'))

    def test_rejects_audio_channel_mismatch(self):
        with self.assertRaises(ValueError):
            validate_avi(fixture(channels=1))

    def test_rejects_oversized_frame(self):
        with self.assertRaises(ValueError):
            validate_avi(fixture(payload=b'\xff\xd8' + b'x' * 65536 + b'\xff\xd9'))

    def test_rejects_fake_jpeg(self):
        with self.assertRaises(ValueError):
            validate_avi(fixture(payload=b'\xff\xd8bogus\xff\xd9'))


if __name__ == '__main__':
    unittest.main()
