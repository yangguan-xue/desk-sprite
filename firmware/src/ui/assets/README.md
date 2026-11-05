# Sakura 页面资源

- 角色直接使用仓库的 `assets/concepts/sakura-green/idle-demo/idle-open-128-source.png` 和 `idle-blink-128-source.png`，各 67×120，保留原图与透明度。页面以最近邻显示为 78×140，并做一像素呼吸动画。
- 字体为 Google Fonts 的 Noto Sans SC，SIL Open Font License 1.1，许可证随附于 `NotoSansSC-OFL.txt`。
- 字库为 12px，变量字体字重 500，单色阈值 100。固定文案、ASCII 与常用通知字预生成；当前共 217 字，未收录字显示 `□`。任意中文通知若需完整显示，须扩充字集后重建。
- `sakura_assets.h` 由脚本生成，包含 RGB565、独立透明遮罩、排序后的点阵字库；无需在设备中解析 PNG 或 TTF。

从 Google Fonts 仓库 `google/fonts` 的 `ofl/notosanssc/` 下载 `NotoSansSC[wght].ttf` 和 `OFL.txt`，放入被 Git 忽略的构建目录（TTF 不提交）。安装 Pillow 后，在仓库根目录运行：

```sh
python tools/generate_sakura_assets.py --font firmware/build/sakura-font/NotoSansSC.ttf
```

扩充字集可编辑生成脚本的 `UI_TEXT`；渲染代码里的中文也会自动纳入。生成后运行 `tools/test_sakura_ui.py` 核对实际输出。
