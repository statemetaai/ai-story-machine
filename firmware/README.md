# ESP32-S3 故事机固件 v0.3

引脚配置见下表，不做改动。先跑通硬件，再接入 AI。

## 引脚（和 `../hardware/ESP32-S3_PLAN.md` 一致）

| 模块 | 引脚 |
|---|---|
| INMP441 麦克风（I2S0 收） | WS=GPIO4，SCK=GPIO5，SD=GPIO6，L/R=GND |
| MAX98357A 功放（I2S1 发） | DIN=GPIO7，BCLK=GPIO15，LRC=GPIO16 |
| SPI-LCD（ST7789） | SCL=GPIO21，SDA=GPIO47，RES=GPIO45，DC=GPIO40，CS=GPIO41，BLK=GPIO42 |
| 按钮 | 板载 BOOT=GPIO0，短按 = 下一个本地故事；长按 1 秒 = 语音输入 |

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
如果开发板不是 DevKitC，只需在 `platformio.ini` 里改 `board = ...`，引脚不用动。

也可以用 Arduino IDE：新建工程，把 `src/main.cpp` 的内容拷进去，库管理器装 `LovyanGFX` 和 `ArduinoJson`，板子选 ESP32-S3，PSRAM 选 OPI PSRAM。

## 上电点亮顺序（H1–H4）

1. **H1 屏幕**：上电，屏幕显示“📖 故事机 / 正在启动... / H1 LCD OK”。不亮先查 3V3、GND、CS/DC/RES。
2. **H2 功放**：启动后听到“523-659-784”三声提示音，屏幕显示 H2 OK。没声先查 DIN/BCLK/LRC、SD 是否接 Vin、喇叭线。
3. **H3 麦克风**：串口每 500ms 打印 `Mic level=...`，对着麦克风说话数字应该明显变大。一直是 0 先查 SD/WS/SCK、L/R 是否接 GND。
4. **H4 本地故事**：屏幕显示第一个故事《小猫咪咪的湖边冒险》，按 BOOT 键换下一个故事，同时响提示音。不联网也能讲。

## TTS / STT 配置（H5）

TTS 和 STT 都可以配置，配置都在 `include/config.h`，密钥在 `include/secrets.h`。

### 配置步骤

1. 复制 `include/secrets.h.template` 为 `include/secrets.h`，填入 WiFi 和 API Key。`secrets.h` 已加入 `.gitignore`，不会提交。
2. 在 `include/config.h` 中选择提供商：

| 功能 | 选项 | 说明 |
|---|---|---|
| STT（语音转文字） | `STT_NONE`（默认） | 不启用语音输入，只用按钮 |
|  | `STT_OPENAI_WHISPER` | OpenAI Whisper API，模型 `whisper-1`，16kHz WAV |
|  | `STT_AZURE` | 预留，未实现 |
| TTS（文字转语音） | `TTS_NONE`（默认） | 不启用云端合成，只播提示音 + 屏幕显示文字 |
|  | `TTS_OPENAI` | OpenAI TTS API，模型 `tts-1`，语音 `alloy`，格式 `wav`，直接取 PCM 播放 |
|  | `TTS_AZURE` | 预留，未实现 |
| LLM（故事生成） | `LLM_NONE`（默认） | 只用本地模板故事 |
|  | `LLM_OPENAI` | OpenAI Chat Completions，模型 `gpt-4o-mini` |

3. 其他可调参数也在 `config.h`：语言（`zh-CN`）、TTS 语音、语速、音量（建议不超过 80）、采样率（16kHz）、VAD 阈值、单次录音最长 8 秒、静音 1.5 秒停止。

### 工作流程（启用后）

```
长按 BOOT 键 1 秒 → 录音（最长 8 秒，静音 1.5 秒停）
  → STT 转文字 → LLM 生成故事 → TTS 合成语音 → I2S 播放
任一步失败 → 自动回落到本地模板故事 + 提示音，并在串口说明原因
```

### 安全与隐私

- 未启用提供商时，设备不连接 WiFi、不上传录音、不调用任何云端 API。
- API Key 只放在本地的 `secrets.h`，不要写进代码、不要提交到仓库。
- 录音只在按住按钮期间采集，存在 PSRAM 中，用完即释放，不保存到 Flash。

## 语音唤醒：唤醒词“小可爱”

唤醒词在 `include/config.h` 中配置：`WAKE_WORD "小可爱"`，`WAKE_MODE` 选模式。

| 模式 | 说明 | 适用 |
|---|---|---|
| `WAKE_NONE`（默认） | 不常听，只用按钮 | 默认，不耗电、不上传 |
| `WAKE_CLOUD_VERIFY` | 麦克风常听，检测到声音后录 2 秒，发 STT 验证是否含“小可爱” | 今天就能用，需配置 STT（Whisper） |
| `WAKE_LOCAL_WAKENET` | 本地离线 WakeNet 检测 | 需先训练模型，见下 |

唤醒后流程：响一声唤醒提示音（880Hz + 1046Hz），屏幕显示“我在听”，10 秒内说出指令（如“讲一个小猫去太空的故事”），设备自动录音、识别、生成、播放；10 秒没说话自动回待机。唤醒后 3 秒内不再重复触发。

### 云端验证模式的取舍（说清楚）

- 优点：不用训练模型，换唤醒词只需改 `WAKE_WORD`。
- 缺点：唤醒有 1-2 秒延迟；只有检测到声音后录的 2 秒片段会上传到 STT 服务验证，平时不上传。
- 阈值：`WAKE_VAD_THRESHOLD`（默认 1500）和 `loop()` 里的电平阈值（`> 5`），按串口 `Mic level` 的实际值调：环境太吵容易误触发，调高；喊了没反应，调低。

### 本地 WakeNet 模式（离线，待训练模型）

1. 按 Espressif ESP-SR 文档，用 WakeNet 训练工具，以“小可爱”为关键词训练自定义唤醒模型（需要准备多段不同人、不同距离的“小可爱”录音）。
2. 把训练出的模型文件放入固件，在 `src/main.cpp` 的 `wakenetDetect()` 里调用 ESP-SR 的检测接口，替换现在的空实现。
3. 在 `config.h` 中把 `WAKE_MODE` 改为 `WAKE_LOCAL_WAKENET`。
4. 未提供模型文件时，`wakenetDetect()` 返回 false，自动回落到不唤醒，不会误报。

## 现在能做什么

- 点亮屏幕、功放出声、麦克风采音、按按钮换本地故事（3 个，网页版同款）
- TTS / STT / LLM 均已接入可配置接口，默认关闭；按上节配置后，长按按钮即可走完整语音流程
- 语音唤醒“小可爱”已接入可配置接口，默认关闭；按上节配置后，说唤醒词即可进入“我在听”状态
