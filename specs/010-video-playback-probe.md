# SPEC-010：有声视频实机验证

## 目标与范围

用户已同意先用官方 AVI Player 播一段带声音的短片。以现有 ESP32-S3、8MiB PSRAM、240×240 ST7789 兼容屏及 ES8311 验证播放能力；本轮使用嵌入测试片，不接云端或直播。输出实际播放统计，不将目标帧率视为实测结果。

## 实现契约

- 独立工程 `tools/video_probe/`，复用 `firmware/main/hal/` 中已核验的显示和音频驱动，不更改产品主程序。
- 固定依赖乐鑫 `avi_player` 2.0.0、`esp_jpeg` 1.3.2。前者分离音视频帧，后者解码 JPEG；RGB565 交换字节后按最多16行同步写屏，避免超出既有 SPI 传输缓冲。
- 受信任测试片：240×136、12fps、8秒，MJPEG baseline、PCM signed 16bit little-endian、16000Hz双声道；固定低幅度1200，前后淡入淡出。画面含运动、帧号、红绿蓝色块；逐秒节拍及闪光用于人工同步判断。视频居中、不拉伸。
- 生成器使用 FFmpeg；写入固件前独立核验 RIFF/AVI 头、流格式、帧数、JPEG 边界、音频字节数、最大块长和文件长度。仅接受生成并通过验证的素材；不直接接收任意网络文件。
- 播放3遍，每遍记录帧数、音频字节、解码错误、I2S错误、耗时、平均帧率及平均/最大解码与刷屏耗时。超时或格式异常失败退出，关闭功放。完成标记不等于物理画面/声音验收通过。
- 以现有音频16000Hz双声道I2S输出直接写PCM，保留已通过听感验收的HAL默认DAC寄存器0xBF，以PCM峰值1200限制音量；写入检查返回值和完整字节数。HAL的百分比直接线性映射寄存器，不能当作听感音量百分比。

## 实物边界

- 写入前核对 MAC `d8:3b:da:4a:e9:84`，完整读取8MiB并使用设备端摘要验证。备份、构建、素材、原始串口日志放忽略目录 `.local/video-trial/`，目录0700、敏感文件0600。
- 使用现有分区表，只替换 `0x10000` 的应用区，不改NVS、bootloader、eFuse、未知GPIO或Wi-Fi凭据。
- 实验结束或失败后恢复备份中的应用区并校验，显式释放DTR后复位；检查原应用启动及网络恢复，关闭串口。写入失败且设备仍可通信时优先回滚。
- 不上传素材，不改腾讯云服务。连续网络播放、长期稳定、真实节目源、任意格式播放留待后续规格。

## 验证命令与验收

```sh
.venv/bin/python -m unittest discover -s tests/tools -p 'test_video_asset.py'
.venv/bin/python tools/video_probe/prepare_asset.py --output .local/video-trial/clip.avi
. /Users/liruoxuan/esp/esp-idf/export.sh
idf.py -C tools/video_probe -B "$PWD/.local/video-trial/build" -DSDKCONFIG="$PWD/.local/video-trial/sdkconfig" -DVIDEO_PROBE_ASSET="$PWD/.local/video-trial/clip.avi" build
git diff --check
```

完成分层：素材及构建通过 → 实机3遍播放统计通过 → 原应用恢复通过 → 用户目视、听感确认。未取得的证据明确标记待确认。C代码保持现有4空格缩进、`ESP_ERROR_CHECK()`初始化风格；仅低层无状态素材验证使用宿主测试，播放性能通过真实硬件测量。

官方来源：[AVI Player](https://github.com/espressif/esp-iot-solution/tree/master/components/avi_player)、[Tiny JPEG Decoder](https://github.com/espressif/idf-extra-components/tree/master/esp_jpeg)。
