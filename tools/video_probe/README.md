# 有声短片实机验证

对应 [SPEC-010](../../specs/010-video-playback-probe.md) 和 [TASK-025](../../tasks/025-video-playback-probe.md)。这是独立测试工程，使用乐鑫 AVI Player 2.0.0 和 esp_jpeg 1.3.2，复用现有屏幕、音频 HAL。

固定测试片为 240×136、12fps、8 秒 MJPEG + 16000Hz 双声道 PCM16，画面有彩条、移动小球和每秒闪光，声音为每秒一次的 660Hz 短音。PCM 峰值限制为 1200；保留此前通过实物验收的 HAL DAC 设置 0xBF。HAL 音量百分比直接映射寄存器，与听感百分比不同。

## 准备与构建

在仓库根目录执行，需 ESP-IDF 5.1.x、Python、Pillow、imageio-ffmpeg；宿主刷写脚本还需要 pyserial 和 esptool 4.x（IDF Python 已包含）。FFmpeg 路径由 imageio-ffmpeg 提供。

```sh
.venv/bin/python -m pip install Pillow imageio-ffmpeg
.venv/bin/python -m unittest discover -s tests/tools -p 'test_video_asset.py'
.venv/bin/python tools/video_probe/prepare_asset.py --output .local/video-trial/clip.avi
.venv/bin/python tools/video_probe/prepare_asset.py --output .local/video-trial/clip.avi --validate-only
. /Users/liruoxuan/esp/esp-idf/export.sh
idf.py -C tools/video_probe -B "$PWD/.local/video-trial/build" \
  -DSDKCONFIG="$PWD/.local/video-trial/sdkconfig" \
  -DVIDEO_PROBE_ASSET="$PWD/.local/video-trial/clip.avi" build
```

素材与构建文件存放在忽略目录中。只使用生成器产生并校验通过的测试片；上游 AVI 解析器不用于接受任意外部文件。视频居中显示，屏幕上下保留黑边。

## 实机执行及恢复

使用当前已核验设备 `/dev/cu.usbmodem11201`，运行前关闭其他串口监视器。输出目录必须是新目录。不要使用 `idf.py flash`，它会同时刷写其他分区；下列脚本只替换应用区。

```sh
/Users/liruoxuan/.espressif/python_env/idf5.1_py3.11_env/bin/python \
  tools/video_probe/run_trial.py \
  --port /dev/cu.usbmodem11201 \
  --build .local/video-trial/build \
  --output .local/video-trial/run-02
```

脚本核对 MAC，完整读取并验证 8MiB flash，检查分区表相同，保存原应用，然后只写 `0x10000` 应用区。复位后等待 5 秒，播放三遍，记录帧数、PCM 字节数、耗时和错误。正常完成或异常时通过 `finally` 恢复原 3MiB 应用并验证，复位后检查原固件启动与 MQTT 订阅，最后关闭串口。

完整备份和串口日志可能含设备凭据，输出目录权限为 0700、文件为 0600，不提交 Git。若主机断电或进程被强制终止，`finally` 无法保证执行；保留 `original-app.bin`，设备重新连接后用 IDF Python 的 esptool 向 `0x10000` 写回并执行 `verify_flash`，勿擦除 flash。

`result.json` 的 `trial_ok` 只表示计数及输出调用通过；屏幕外观、声音、同步需用户确认。平均 fps 包括音频排空等待，不能作为最高解码速度或网络播放性能。串口原始日志留在本地，去敏结果写入 `docs/hardware/`。

官方来源：[AVI Player](https://github.com/espressif/esp-iot-solution/tree/master/components/avi_player)、[JPEG Decoder](https://github.com/espressif/idf-extra-components/tree/master/esp_jpeg)、[ES8311 音量范围](https://github.com/espressif/esp-adf/blob/master/components/esp_codec_dev/device/es8311/es8311.c)。
