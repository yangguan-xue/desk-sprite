# 樱花汽水模拟器

ESP32 与 SDL2 使用同一个 `ui_sakura.c` 渲染器，输出 240×240 RGB565 像素。无需网页服务。

## 本地运行

Linux / macOS / WSL 安装 C99 编译器、SDL2 开发库和 pkg-config 后，在仓库根目录执行：

```sh
make -C firmware/sim test_build
firmware/sim/desk_sprite_sim --standalone --mode idle
```

`--mode` 支持 `idle`、`coding`、`meeting`、`sleep`。`--standalone` 使用明确标为「演示」的 68% 剩余额度；正常联网模式等待 MQTT 数据，没有数据时显示 `--`。单击切换数值/百分比，长按切换临时静音；通知期间沿用告警交互。

仅检查 SDL 渲染路径（无显示器）：

```sh
SDL_VIDEODRIVER=dummy firmware/sim/desk_sprite_sim --standalone --once --mode coding
```

## 导出与测试

```sh
python3 tools/test_sakura_ui.py --cc cc --sanitize
```

此命令使用与设备相同的渲染代码检查额度语义、缓冲区边界、UTF-8 输入，并导出 idle / focus / meeting / night / done / alert / stale / empty / blink / invalid-utf8 十种状态。图中的时钟和额度为固定演示值。

输出位于被 Git 忽略的 `firmware/build/sakura-preview/`。默认生成 PPM；当前 Python 环境安装 Pillow 后，还会生成 PNG、720px 最近邻放大图、`states.png` 拼图和眨眼 GIF。`--sanitize` 需要支持 AddressSanitizer/UndefinedBehaviorSanitizer 的编译器。

角色与字体来源、重建方式见 [资源说明](../src/ui/assets/README.md)。完整固件编译需另行配置 ESP-IDF；模拟器通过不代表实机显示已验收。本次未烧录设备。
