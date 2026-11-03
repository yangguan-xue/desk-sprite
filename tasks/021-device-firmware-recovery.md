# TASK-021：恢复原 Kimi 额度看板

- 输入：[SPEC-006](../specs/006-device-firmware-recovery.md)、同级项目本地镜像、USB 设备。
- 交付物：本地当前固件回滚备份、恢复后校验结果、去敏恢复记录、更新后的实机规格。
- 约束：不猜 GPIO；不输出凭证；不把启动成功当实屏成功；不覆盖现有固件源代码改动。

## 执行与验证

1. `esptool --chip esp32s3 --port /dev/cu.usbmodem11201 --baud 460800 read_flash 0 0x800000 <本地备份路径>`。
2. 核对备份长度、SHA256，执行 `verify_flash 0 <本地备份路径>`，失败则停止写入。
3. 检查恢复镜像 chip_id、bootloader/app 校验、分区范围和预期 SHA256。
4. 使用 `write_flash 0 <已核验恢复镜像>`，保留镜像 Flash 参数并检查写入时全片校验；运行后按区域核对，允许 NVS 正常变化。诊断失败也须保证最终复位回应用、释放串口。
5. 采集去敏启动结论，询问用户实物屏幕；记录未通过项目，不宣称全部完成。

验收状态见 `docs/hardware/2026-11-03-usb-inspection/recovery.md`。
