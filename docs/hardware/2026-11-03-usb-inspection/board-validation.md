# 小电视板级核验结果

- 日期：2026-11-03
- 契约：[SPEC-007](../../../specs/007-board-hardware-validation.md)、[TASK-022](../../../tasks/022-board-hardware-validation.md)
- 结论：主控、存储、显示总线和音频播放已有实机证据；四边粗框/底部四色带获照片确认。新取得的卖家S3整板图补齐触摸GPIO5、录音GPIO15和背光低有效电路。GPIO5原生电容600次读取完成，但按下/松开关联未通过，产品触摸仍禁用。该轮结束用户确认回到Kimi看板，串口关闭；长期稳定性仍未验收。

## 1. 当前开发基线

| 部件 | 参数 / GPIO | 证据与限制 |
| --- | --- | --- |
| SoC | ESP32-S3 rev0.2，双核240MHz，40MHz晶振 | 之前 esptool / 启动实测 |
| Flash / PSRAM | 8MiB / 8MiB OPI | 容量、PSRAM80MHz及自检已测 |
| 显示 | ST7789兼容命令集，240×240，RGB565 | 恢复 Kimi 看板可见；独立 RAM 驱动彩条实物可见；控制器具体后缀未读 ID |
| 显示 SPI | MOSI1、SCLK18、DC17、RST6；CS=-1；Mode3、27MHz | 独立 RAM 驱动成功；CS=-1仅表示软件不控制片选，不证明物理接地 |
| 背光 | GPIO2，测试PWM5kHz/8bit、raw205；AO3401高边开关 | 原理图推导低有效，与卖家反向PWM源码一致；亮度曲线、极性变化未实测，raw205不等于80%亮度 |
| 音频 Codec | ES8311，I2C0x18，SDA40、SCL39、100kHz | 两轮读取FD=83、FE=11、FF=00；不能仅凭I2C ACK判型号 |
| 音频 I2S | MCLK38、BCLK16、WS7、DOUT4 | 已配置、传输成功，用户两次确认听到提示音 |
| 录音 | ES8311 ASDOUT → GPIO15 | 新取得原理图证据；录音未测 |
| 播放格式 | 16kHz、16bit，同值双声道；MCLK4.096MHz | 本次驱动配置及可听测试；频率未用仪器测量 |
| 功放控制 | GPIO8，高电平播放、低电平停止 | 此路径提示音可听；S3原理图标注NS4150B，未读芯片丝印 |
| 麦克风 / 扬声器 | 图纸标注GMI4015P-2C-42db / FS-2308 | 未核对实物丝印；扬声器阻抗/功率未知 |
| 触摸 | 顶部单键；S3原理图TP_KEY直连GPIO5，使用原生电容测量 | 600次原始读取完成，交互仍未通过；严禁把GPIO4当触摸 |
| USB | 原生USB Serial/JTAG，VID303a/PID1001，12Mbps | 已枚举；用户动线时本次出现真实USB掉线 |

机器可读清单：[board-profile.json](board-profile.json)。此清单是后续适配依据，尚未自动接入产品固件。

## 2. 固件来源对应关系

固定 QuotaClock commit `defb2c83986de0bf88b826a716f0d0415315d2f4`。

- 公开镜像SHA256：`edb6872b0d6794ad5209cefbe7c53a3f606eb9145691a5a3bb41bc4c13ba3ae7`。
- 本地恢复镜像SHA256：`b80ab84c9d62ac119ca38e395f23266bfceb542859599597e012ecbd661645a6`。
- 全片仅69字节不同：36字节显示标签，1字节镜像XOR校验，32字节附加SHA256。标签从OpenAI/Cursor改为Kimi；其它字节一致，包括驱动指令和引脚常量。
- boot、分区、NVS空白模板、otadata、资源区全部相同。源码归档并非可复现构建证明，但对应关系已强于版本字符串匹配，并以本次独立外设测试补足。
- 对比证据：[source-image-match.json](source-image-match.json)。公开镜像只用于对比，没有刷入实机。

关键源码：[User_Setup.h](https://github.com/akechi1980/QuotaClock/blob/defb2c83986de0bf88b826a716f0d0415315d2f4/vendor/clock_rom/libraries/TFT_eSPI/User_Setup.h)、[TFT_eSPI.h](https://github.com/akechi1980/QuotaClock/blob/defb2c83986de0bf88b826a716f0d0415315d2f4/vendor/clock_rom/libraries/TFT_eSPI/TFT_eSPI.h)、[初始化](https://github.com/akechi1980/QuotaClock/blob/defb2c83986de0bf88b826a716f0d0415315d2f4/vendor/clock_rom/libraries/TFT_eSPI/TFT_Drivers/ST7789_2_Init.h)、[音频](https://github.com/akechi1980/QuotaClock/blob/defb2c83986de0bf88b826a716f0d0415315d2f4/clock_rom/mono_audio.h)。

## 3. 网络配置试验及回滚

恢复前8MiB备份中旧NVS页头、有效entry CRC校验通过，存在Wi-Fi配置；当前NVS先读取、私有保存并设备端MD5核验，再只恢复20KiB旧NVS。未定位到`clock-progress.local`，也未获得HTTP状态；不能宣称联网正常或Kimi额度是真实最新值。

该试验已经回滚到试验前NVS，写后设备端校验通过。boot、分区、otadata、app、资源区均与已恢复Kimi镜像相符。NVS运行后可正常变化，不要求整片Flash永久逐字节相等。

证据：[nvs-probe-result.json](nvs-probe-result.json)、[final-verification.json](final-verification.json)。旧配置及本轮各NVS快照仅在`kimi-pocket/firmware_backup/`忽略目录，权限600，不在公开记录中保存SSID、密码、access或CSRF。

## 4. RAM诊断与实物验收

诊断独立构建为ESP-IDF RAM应用，由ROM loader写入SRAM执行；不写Flash。每次结束尝试复位并释放串口，复位方法返回不等于确认应用启动；第四轮回到Kimi由用户观察确认。第三轮完整运行60秒、空闲堆264812B保持不变。测试用ES8311初始化序列来自匹配的旧源码，仅播放低幅度880Hz单声。旧日志`volume=20`是诊断标签，不是实测响度或PCM幅度百分比；实际PCM峰值为1200/32767。

| 轮次 | 结果 |
| --- | --- |
| 首轮30秒 | 身份读取和I2S写入通过；用户未观察完整，要求重做 |
| 第二轮60秒 | 用户听到声音，但黑屏有背光；临时驱动误用Mode0，不能据日志成功验收显示。GPIO `%llx`被nano printf错误格式化，电平日志不可用 |
| 第三轮60秒 | 对齐TFT_eSPI的Mode3与等待时序；用户听到声音，确认左红中绿右蓝、上左右白框，底部1px白框未见。显示主要功能通过，底边当时未验收 |
| 第四轮加粗边框 | 改8px边框和底部紫/青/黄/白色带；不重复发声。运行约15秒后USB被系统报告断开，用户说动到了线、屏幕回到Kimi，测试无效，随后已完成重测 |
| 第五轮边界重测60秒 | 用户回复“都弄了”并发照片；四边粗白框、底部紫青黄白色带可见。仅捕获GPIO3/9同时300ms脉冲一次，不能对应三次触摸 |
| 第六轮触摸重测60秒 | 无GPIO变化；用户随后说“我刚没看到”，未完成配合，不作为有效触摸试验 |
| 第七轮配合触摸60秒 | 用户确认“完成，顶部”；按住2秒/松开3秒三次，无GPIO变化。触摸接线及技术仍未知，不认定按键损坏 |
| 第八轮原生触摸60秒 | 取得原理图后仅配置GPIO5/TOUCH_PAD_NUM5；600次读取无错误，计数16332～16400；用户确认完成三次并回到Kimi。尚未建立可靠按下/松开关联 |

证据：[首轮](ram-probe-result.json)、[第二轮](ram-probe-repeat-result.json)、[Mode3轮](ram-probe-mode3-result.json)、[边界中断轮](ram-probe-boundary-result.json)。新增证据：[边界重测](ram-probe-boundary-retry-result.json)、[错过配合轮](ram-probe-touch-repeat-result.json)、[确认触摸轮](ram-probe-touch-coordinated-result.json)、[用户照片的设备区域裁切](display-boundary-observed.jpg)。实物反馈以本表为准，驱动日志不替代实物。照片支持粗边界通过，不能确认每一像素均未裁切。

### 4.1 输入核验边界

只读取输入寄存器，没有配置未知GPIO的方向、上下拉、复用或输入使能。排除了显示、音频、USB与存储引脚。第三轮只有初始电平`0x1`，没有变化；用户未明确确认完成三次触摸，因此不能认定触摸无响应或不存在。

第四轮输入使能掩码`0x0001e60000207e09`，记录前15秒同样没有电平变化；该轮被物理连接中断。读数字输入不能排除原生电容触摸、未使能输入路径或外部触摸芯片。第五轮用户报告已触摸三次，但仅捕获GPIO3/9同时变高再恢复一次（17.6～17.9秒）；配合触摸轮未复现。不能认定两者是触摸接线。第七轮用户明确完成顶部按住2秒/松开3秒三次，仍无数字输入变化，说明当前被动读取不足以识别触摸；不能据此认定硬件损坏。

新取得的S3图纸把TP_KEY直连IO5，未画TTP223。第八轮改为仅GPIO5原生电容测量，600次返回ESP_OK；前5秒均值16377.27，全程范围16332～16400。用户确认完成并回到Kimi，但细小变化与三次动作缺少可靠时间关联，不能标为触摸通过或认定损坏。来源、方法及限制见[卖家资料核对](seller-materials-review.md)、[原始记录](ram-probe-native-touch-result.json)、[统计与用户确认](native-touch-assessment.json)。

### 4.2 黑屏与连接稳定性

早先恢复中确实发生过诊断留下载模式；这能解释部分黑屏，不能解释全部。此次Mode0黑屏属于临时诊断不兼容；Mode3已实屏验证。另有一条独立证据：用户动线时RAM程序在15秒出现`SerialException: [Errno 6] Device not configured`，早于60秒自动返回，确认USB链路中断。

线材、插头、Hub或整机供电接触需排查，尚不能定位到哪个部件。不能因此声称历史自动灭屏根因已全部找到。NVS回滚后另有65.2秒监听，无panic/brownout文字或重复ROM启动；Arduino固件日志较少，只有20字节，证据较弱。用户此前曾确认75秒监听结束后仍保持亮屏。连接恢复后新增三轮各60秒RAM诊断，随后又完成本轮60秒原生触摸诊断，均收到结束标记、无串口异常且已关闭端口；这属于短时连接证据，不是长期供电或自动灭屏问题已解决的证明。

## 5. 下一步开发准入

1. **可进行显示、音效的板级驱动适配**：严格使用本次GPIO与Mode3；产品现有HAL未修改，引脚仍错误，不能直接刷现有Desk Sprite构建。
2. **粗边界已通过**：8px四边框与底部四色带照片可见；单像素有效范围、其它旋转方向仍未逐项校准。不能把8px测试框当面板安全边距规格。
3. **触摸驱动可按GPIO5原生电容方案继续诊断**：原理图接线已取得；需校准灵敏度、重复验证按下/松开及手势，产品入口继续禁用，用户动作上报闭环尚未通过。GPIO4不可复用。
4. **供电连接先稳定**：固定连接、必要时换线/绕过Hub，对比动线和静置；不要在连接不稳时写Flash。
5. 屏幕具体料号、英寸/IPS仍未匹配：两份12针候选屏引脚顺序与整板图不一致，不能直接认定或替换。麦克风、功放、扬声器已有图纸标号，实物装配/录音、扬声器阻抗功率、整机电池与功耗仍未核验。

卖家`S3天气预报电路图.pdf`原件现已取得，SHA256 `840b5900d0ba32935f07a8d8fa5649377ef34da617d5730c5b82056648db7a76`；核对结果见[资料审阅记录](seller-materials-review.md)。图纸声明不等于本机全板实测。
