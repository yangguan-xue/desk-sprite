#!/usr/bin/env python3
"""Generate and validate the trusted SPEC-010 MJPEG/PCM test clip."""
import argparse
import hashlib
import io
import json
import math
from pathlib import Path
import struct
import subprocess
import tempfile
import wave

WIDTH, HEIGHT, FPS, SECONDS = 240, 136, 12, 8
MAX_CHUNK = 64 * 1024


def chunks(data, start, end):
    while start < end:
        if start + 8 > end:
            raise ValueError('Truncated chunk header')
        kind, size = struct.unpack_from('<4sI', data, start)
        pos = start + 8
        next_pos = pos + size + size % 2
        if next_pos > end:
            raise ValueError('Chunk exceeds container')
        yield kind, pos, size
        start = next_pos


def validate_avi(data):
    from PIL import Image

    if len(data) < MAX_CHUNK or data[:4] != b'RIFF' or data[8:12] != b'AVI ':
        raise ValueError('Expected a complete AVI larger than the player header buffer')
    if struct.unpack_from('<I', data, 4)[0] + 8 != len(data):
        raise ValueError('RIFF size mismatch')
    streams, frames, audio_bytes, max_chunk = [], 0, 0, 0
    movi_found = False
    for kind, pos, size in chunks(data, 12, len(data)):
        if kind != b'LIST':
            continue
        if data[pos:pos + 4] == b'hdrl':
            for subkind, sp, ss in chunks(data, pos + 4, pos + size):
                if subkind == b'avih':
                    if ss != 56:
                        raise ValueError('Unexpected main header')
                    header = struct.unpack_from('<14I', data, sp)
                    if header[4] != FPS * SECONDS or header[6] != 2 or header[8:10] != (WIDTH, HEIGHT):
                        raise ValueError('Unexpected clip duration, streams or dimensions')
                if subkind != b'LIST' or data[sp:sp + 4] != b'strl':
                    continue
                fields = {k: data[p:p + s] for k, p, s in chunks(data, sp + 4, sp + ss)}
                h, f = fields.get(b'strh', b''), fields.get(b'strf', b'')
                if len(h) != 56:
                    raise ValueError('Invalid stream header')
                if h[:4] == b'vids':
                    if h[4:8] != b'MJPG' or len(f) != 40:
                        raise ValueError('Only baseline MJPEG is supported')
                    scale, rate = struct.unpack_from('<II', h, 20)
                    if (scale, rate) != (1, FPS) or struct.unpack_from('<ii', f, 4) != (WIDTH, HEIGHT):
                        raise ValueError('Unexpected video format')
                elif h[:4] == b'auds':
                    if len(f) not in (16, 18) or struct.unpack_from('<HHIIHH', f) != (1, 2, 16000, 64000, 4, 16):
                        raise ValueError('Expected 16kHz stereo PCM16')
                else:
                    raise ValueError('Unsupported stream')
                streams.append(h[:4])
        elif data[pos:pos + 4] == b'movi':
            movi_found = True
            if pos + 4 > MAX_CHUNK:
                raise ValueError('AVI header exceeds player buffer')
            for ck, cp, cs in chunks(data, pos + 4, pos + size):
                if cs == 0 or cs + cs % 2 > MAX_CHUNK:
                    raise ValueError('Empty or oversized media chunk')
                max_chunk = max(max_chunk, cs)
                if ck == b'00dc':
                    image_data = data[cp:cp + cs]
                    if not image_data.startswith(b'\xff\xd8') or not image_data.endswith(b'\xff\xd9'):
                        raise ValueError('JPEG marker mismatch')
                    try:
                        with Image.open(io.BytesIO(image_data)) as img:
                            if img.size != (WIDTH, HEIGHT) or img.info.get('progressive'):
                                raise ValueError('Unsupported JPEG')
                            img.load()
                    except OSError as exc:
                        raise ValueError('Invalid JPEG') from exc
                    frames += 1
                elif ck == b'01wb' and cs % 4 == 0:
                    audio_bytes += cs
                else:
                    raise ValueError('Unsupported media chunk')
    if not movi_found or streams != [b'vids', b'auds'] or frames != FPS * SECONDS or audio_bytes != SECONDS * 64000:
        raise ValueError('Missing or incomplete video/audio streams')
    return {'width': WIDTH, 'height': HEIGHT, 'fps': FPS, 'seconds': SECONDS,
            'video_frames': frames, 'audio_bytes': audio_bytes, 'max_chunk': max_chunk,
            'file_bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


def generate(output):
    from PIL import Image, ImageDraw
    import imageio_ffmpeg

    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as td:
        audio = Path(td) / 'ticks.wav'
        samples = bytearray()
        for n in range(16000 * SECONDS):
            p = n % 16000
            env = max(0, min(1, p / 160, (1600 - p) / 160))
            v = round(1200 * env * math.sin(2 * math.pi * 660 * n / 16000))
            samples.extend(struct.pack('<hh', v, v))
        with wave.open(str(audio), 'wb') as wav:
            wav.setparams((2, 2, 16000, 0, 'NONE', 'not compressed'))
            wav.writeframes(samples)
        cmd = [imageio_ffmpeg.get_ffmpeg_exe(), '-y', '-loglevel', 'error',
               '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', f'{WIDTH}x{HEIGHT}',
               '-r', str(FPS), '-i', '-', '-i', str(audio), '-map', '0:v', '-map', '1:a',
               '-c:v', 'mjpeg', '-q:v', '5', '-pix_fmt', 'yuvj420p', '-threads:v', '1',
               '-c:a', 'pcm_s16le', '-ar', '16000', '-ac', '2',
               '-fflags', '+bitexact', '-flags:v', '+bitexact', '-map_metadata', '-1', str(output)]
        proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)
        try:
            for frame in range(FPS * SECONDS):
                img = Image.new('RGB', (WIDTH, HEIGHT), '#142035')
                d = ImageDraw.Draw(img)
                for x, c in zip((0, 80, 160), ('#ef3030', '#30dc50', '#3050ef')):
                    d.rectangle((x, 0, x + 79, 17), fill=c)
                x = 10 + (frame * 5) % 200
                y = 58 + round(15 * math.sin(frame / 5))
                d.ellipse((x, y, x + 20, y + 20), fill='#ffcf60')
                d.text((12, 26), 'DESK SPRITE / VIDEO + AUDIO', fill='white')
                d.text((12, 102), f'{frame:03d}/096  12FPS  {frame / FPS:.2f}s', fill='white')
                d.rectangle((0, 129, round(WIDTH * (frame + 1) / (FPS * SECONDS)), 135), fill='#ef82b1')
                if frame % FPS == 0:
                    d.rectangle((207, 44, 233, 70), fill='white')
                proc.stdin.write(img.tobytes())
        finally:
            proc.stdin.close()
            if proc.wait() != 0:
                raise RuntimeError('FFmpeg failed')
    return validate_avi(output.read_bytes())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--validate-only', action='store_true')
    a = p.parse_args()
    result = validate_avi(a.output.read_bytes()) if a.validate_only else generate(a.output)
    a.output.with_suffix('.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
