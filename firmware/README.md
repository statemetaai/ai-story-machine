# ESP32-S3 故事机固件 v0.1（H1–H4 骨架）

引脚配置见下表，不做改动。先跑通硬件，再接入 AI（H5）。

## 引脚（和 `../hardware/ESP32-S3_PLAN.md` 一致）

| 模块 | 引脚 |
|---|---|
| INMP441 麦克风（I2S0 收） | WS=GPIO4，SCK=GPIO5，SD=GPIO6，L/R=GND |
| MAX98357A 功放（I2S1 发） | DIN=GPIO7，BCLK=GPIO15，LRC=GPIO16 |
| SPI-LCD（ST7789） | SCL=GPIO21，SDA=GPIO47，RES=GPIO45，DC=GPIO40，CS=GPIO41，BLK=GPIO42 |
| 按钮 | 板载 BOOT=GPIO0，短按 = 下一个故事 |

> GPIO45 是启动配置脚，固件在启动完成之后才操作 RES 脚。本硬件按此接法实测可启动，按此配置执行。

## 怎么编译烧录（PlatformIO）

```bash
# 1. 装 PlatformIO（VS Code 里装 PlatformIO IDE 扩展最方便）
# 2. 打开 firmware/ 文件夹
# 3. 编译 + 烧录：
pio run -t upload
# 4. 看串口日志：
pio device monitor -b 115200
```

板子选 `esp32-s3-devkitc-1`，框架 Arduino，PSRAM 开启。
如果你的开发板不是 DevKitC，只是换板型名，`platformio.ini` 里改 `board = ...` 即可，引脚不用动。

也可以用 Arduino IDE：新建工程，把 `src/main.cpp` 的内容拷进去，库管理器装 `LovyanGFX`，板子选 ESP32-S3，PSRAM 选 OPI PSRAM。

## 上电点亮顺序（H1–H4）

1. **H1 屏幕**：上电，屏幕显示“📖 故事机 / 正在启动... / H1 LCD OK”。不亮先查 3V3、GND、CS/DC/RES。
2. **H2 功放**：启动后听到“523-659-784”三声提示音，屏幕显示 H2 OK。没声先查 DIN/BCLK/LRC、SD 是否接 Vin、喇叭线。
3. **H3 麦克风**：串口每 500ms 打印 `Mic level=...`，对着麦克风说话数字应该明显变大。一直是 0 先查 SD/WS/SCK、L/R 是否接 GND。
4. **H4 本地故事**：屏幕显示第一个故事《小猫咪咪的湖边冒险》，按 BOOT 键换下一个故事，同时响提示音。不联网也能讲。

## 现在能做什么 / 还不能做什么

- ✅ 能：点亮屏幕、功放出声、麦克风采音、按按钮换本地故事（3 个，网页版同款）
- ❌ 还不能：联网 AI 生成故事、语音识别、TTS 播报 —— 那是 H5，见下

## 下一步 H5（接真 AI）

- WiFi 配网（先写死家里 WiFi，后面再做配网页）
- 录音 → 发给 AI 服务 → 拿回故事文本 → TTS 合成音频 → I2S 播放
- 断网时自动回落到本地模板故事（就是现在这 3 个）
- H5 接入 AI 时确定服务商；API Key 通过安全通道提供，不要写进代码或提交到 GitHub。
