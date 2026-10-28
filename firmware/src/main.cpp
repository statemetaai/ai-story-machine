// 亲子 AI 故事机 · ESP32-S3 固件 v0.3
// 引脚配置见下表，不做改动。
//   麦克风 INMP441 (I2S0 收): WS=GPIO4, SCK=GPIO5, SD=GPIO6, L/R=GND(左声道)
//   功放 MAX98357A (I2S1 发): DIN=GPIO7, BCLK=GPIO15, LRC=GPIO16, SD=Vin常开, GAIN=GND
//   屏幕 SPI-LCD (ST7789): SCL=GPIO21, SDA=GPIO47, RES=GPIO45, DC=GPIO40, CS=GPIO41, BLK=GPIO42
//   按钮: 板载 BOOT = GPIO0（短按 = 下一个本地故事；长按 1 秒 = 语音输入）
//
// 注意 GPIO45: 它是启动配置脚，管 Flash 电压。启动瞬间必须是低电平。
// 本固件在 setup() 里、启动完成之后才操作 RES 脚，启动前不碰它。
// 本硬件按此接法实测可启动，按此配置执行。
//
// 阶段: H1 点亮屏幕 / H2 功放出声 / H3 麦克风 / H4 本地故事
//       H5 TTS/STT 可配置（见 include/config.h，默认关闭，配置后启用）
// TTS/STT 均可配置：提供商、语音、语速、采样率、VAD 阈值等见 config.h。
// 未配置提供商时，设备只用本地模板故事和提示音，不联网、不上传录音。

#include <Arduino.h>
#include <driver/i2s.h>
#include <LovyanGFX.hpp>
#include <math.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "config.h"
#include "secrets.h"

// ---------- LCD (LovyanGFX, ST7789) ----------
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel_instance;
  lgfx::Bus_SPI _bus_instance;
public:
  LGFX(void) {
    { // SPI 总线
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 21;   // SCL
      cfg.pin_mosi = 47;   // SDA
      cfg.pin_miso = -1;
      cfg.pin_dc = 40;     // DC
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    { // 面板
      auto cfg = _panel_instance.config();
      cfg.pin_cs = 41;     // CS
      cfg.pin_rst = 45;    // RES —— 启动后才由 LovyanGFX 操作，见上面注释
      cfg.pin_busy = -1;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = false;
      cfg.invert = false;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance);
  }
};

static LGFX lcd;

// ---------- 引脚 ----------
#define PIN_MIC_WS   4
#define PIN_MIC_SCK  5
#define PIN_MIC_SD   6
#define PIN_SPK_DIN  7
#define PIN_SPK_BCLK 15
#define PIN_SPK_LRC  16
#define PIN_LCD_BLK  42
#define PIN_BTN      0   // 板载 BOOT 键

#define SAMPLE_RATE 16000

// ---------- 本地故事（网页版同款，H4 不联网也能讲） ----------
struct Story { const char* title; const char* hero; const char* scene; const char* lesson; const char* body; };
static const Story STORIES[] = {
  {"小猫咪咪的湖边冒险", "小猫咪咪", "湖边", "勇敢",
   "在一个阳光很好的早上，小猫咪咪醒来了。它好奇又爱干净，今天决定去湖边玩一玩。\n\n"
   "走着走着，前面出现了一道难题。小猫咪咪有点紧张，心里砰砰跳。它深吸一口气，决定先试一小步。没想到，试一试之后，办法真的出现了。\n\n"
   "回到家的时候，小猫咪咪开心地说：原来遇到害怕的事，先深呼吸，再勇敢地试一小步。"},
  {"小鹈鹕皮皮的太空之旅", "小鹈鹕皮皮", "太空", "好奇",
   "有一天，小鹈鹕皮皮听说太空里藏着一个小秘密，于是它邀请小朋友一起出发了。\n\n"
   "路上，小鹈鹕皮皮遇到了一个需要帮助的朋友。朋友的东西掉进了水里，它没有走开，而是动脑筋想办法，还请小朋友一起帮忙。两个人齐心协力，终于把东西捞了上来。\n\n"
   "从那以后，每次去太空，小鹈鹕皮皮都会想起今天的冒险。它明白了：多问一个为什么，世界就会多打开一扇门。"},
  {"小老鼠吱吱的森林晚会", "小老鼠吱吱", "森林", "分享",
   "森林里真热闹，有大树、蘑菇和小溪。小老鼠吱吱背上小书包，对小朋友说：走，我们一起去冒险吧！\n\n"
   "忽然，天空飘来一片乌云。小老鼠吱吱没有跑回家，而是打开小伞，和小朋友一起数雨滴、听雨声。等雨停了，森林变得更干净、更漂亮了。\n\n"
   "晚上，小老鼠吱吱躺在床上想：今天我学会了分享。好东西和朋友一起分享，快乐会变成两份。它抱着这个小道理，甜甜地睡着了。"},
};
static const int STORY_COUNT = sizeof(STORIES) / sizeof(STORIES[0]);
static int storyIndex = 0;

// ---------- I2S ----------
static void i2sMicInit() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate = SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT; // INMP441 是 24bit 放在 32bit 槽里
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;  // L/R 接 GND = 左声道
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = false;
  cfg.fixed_mclk = 0;
  i2s_pin_config_t pins = {};
  pins.bck_io_num = PIN_MIC_SCK;
  pins.ws_io_num = PIN_MIC_WS;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = PIN_MIC_SD;
  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
  i2s_set_clk(I2S_NUM_0, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO);
}

static void i2sSpkInit() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  cfg.fixed_mclk = 0;
  i2s_pin_config_t pins = {};
  pins.bck_io_num = PIN_SPK_BCLK;
  pins.ws_io_num = PIN_SPK_LRC;
  pins.data_out_num = PIN_SPK_DIN;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  i2s_driver_install(I2S_NUM_1, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &pins);
  i2s_set_clk(I2S_NUM_1, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
}

// 播一个正弦音，H2 测试功放用
static void playTone(int freqHz, int ms) {
  const int n = SAMPLE_RATE * ms / 1000;
  int16_t buf[256];
  size_t written = 0;
  int pos = 0;
  while (pos < n) {
    int chunk = min(256, n - pos);
    for (int i = 0; i < chunk; i++) {
      double t = (double)(pos + i) / SAMPLE_RATE;
      buf[i] = (int16_t)(12000 * sin(2 * M_PI * freqHz * t));
    }
    i2s_write(I2S_NUM_1, buf, chunk * sizeof(int16_t), &written, portMAX_DELAY);
    pos += chunk;
  }
}
static void playChime() { playTone(523, 120); playTone(659, 120); playTone(784, 200); }

// ---------- WiFi / TTS / STT / LLM（H5，可配置，默认关闭） ----------
// 原则：未在 config.h 中启用提供商时，不连接 WiFi、不上传录音、不调用任何云端 API。
// 启用后：录音 -> STT 转文字 -> LLM 生成故事 -> TTS 合成语音 -> I2S 播放。
// 任一步失败，自动回落到本地模板故事 + 提示音，并在串口和屏幕上说明原因。
static bool wifiConnected = false;

static bool connectWiFi() {
  if (STT_PROVIDER == STT_NONE && TTS_PROVIDER == TTS_NONE && LLM_PROVIDER == LLM_NONE) return false;
  if (String(WIFI_SSID).length() == 0) { Serial.println("[WiFi] 未配置 WIFI_SSID，跳过联网"); return false; }
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) delay(200);
  wifiConnected = (WiFi.status() == WL_CONNECTED);
  Serial.printf("[WiFi] %s\n", wifiConnected ? "已连接" : "连接失败");
  return wifiConnected;
}

// 给 16-bit PCM 加 44 字节 WAV 头，STT 接口需要 WAV 格式
static size_t wavHeader(uint8_t* out, size_t pcmBytes) {
  uint32_t total = pcmBytes + 36;
  memcpy(out, "RIFF", 4);
  memcpy(out + 4, &total, 4);
  memcpy(out + 8, "WAVEfmt ", 8);
  uint32_t v = 16; memcpy(out + 16, &v, 4);
  uint16_t t = 1; memcpy(out + 20, &t, 2);
  t = 1; memcpy(out + 22, &t, 2);
  uint32_t sr = AUDIO_SAMPLE_RATE; memcpy(out + 24, &sr, 4);
  uint32_t br = AUDIO_SAMPLE_RATE * 2; memcpy(out + 28, &br, 4);
  uint16_t ba = 2; memcpy(out + 32, &ba, 2);
  uint16_t bits = 16; memcpy(out + 34, &bits, 2);
  memcpy(out + 36, "data", 4);
  memcpy(out + 40, &pcmBytes, 4);
  return 44;
}

// STT：把录音发给配置的提供商，返回识别出的文字；失败返回 false
static bool transcribeAudio(const int16_t* samples, size_t count, String& outText) {
  if (STT_PROVIDER == STT_NONE || !wifiConnected) return false;
  if (STT_PROVIDER == STT_OPENAI_WHISPER) {
    size_t pcmBytes = count * 2;
    size_t wavBytes = pcmBytes + 44;
    uint8_t* wav = (uint8_t*)ps_malloc(wavBytes);
    if (!wav) return false;
    wavHeader(wav, pcmBytes);
    memcpy(wav + 44, samples, pcmBytes);
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http; http.setTimeout(HTTP_TIMEOUT_MS);
    http.begin(client, String(OPENAI_API_BASE) + "/audio/transcriptions");
    http.addHeader("Authorization", String("Bearer ") + OPENAI_API_KEY);
    String boundary = "----StoryMachineBoundary";
    http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    String head = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n" + STT_OPENAI_MODEL +
      "\r\n--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"speech.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
    String tail = "\r\n--" + boundary + "--\r\n";
    size_t bodyLen = head.length() + wavBytes + tail.length();
    uint8_t* body = (uint8_t*)ps_malloc(bodyLen);
    if (!body) { free(wav); return false; }
    memcpy(body, head.c_str(), head.length());
    memcpy(body + head.length(), wav, wavBytes);
    memcpy(body + head.length() + wavBytes, tail.c_str(), tail.length());
    int code = http.POST(body, bodyLen);
    bool ok = false;
    if (code == 200) {
      JsonDocument doc; deserializeJson(doc, http.getString());
      if (doc["text"].is<const char*>()) { outText = doc["text"].as<String>(); ok = outText.length() > 0; }
    } else { Serial.printf("[STT] HTTP %d\n", code); }
    free(body); free(wav); http.end();
    return ok;
  }
  Serial.println("[STT] 该提供商暂未实现，请在 config.h 中选择 STT_OPENAI_WHISPER 或 STT_NONE");
  return false;
}

// TTS：把文字合成为 WAV PCM 并直接经 I2S 播放；失败返回 false
static bool speakText(const String& text) {
  if (TTS_PROVIDER == TTS_NONE || !wifiConnected) { playChime(); return false; }
  if (TTS_PROVIDER == TTS_OPENAI) {
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http; http.setTimeout(HTTP_TIMEOUT_MS);
    http.begin(client, String(OPENAI_API_BASE) + "/audio/speech");
    http.addHeader("Authorization", String("Bearer ") + OPENAI_API_KEY);
    http.addHeader("Content-Type", "application/json");
    JsonDocument req;
    req["model"] = TTS_OPENAI_MODEL; req["input"] = text;
    req["voice"] = TTS_VOICE; req["response_format"] = TTS_OPENAI_FORMAT; req["speed"] = TTS_SPEED;
    String payload; serializeJson(req, payload);
    int code = http.POST(payload);
    if (code != 200) { Serial.printf("[TTS] HTTP %d\n", code); http.end(); return false; }
    WiFiClient* stream = http.getStreamPtr();
    uint8_t header[44]; size_t got = 0;
    while (got < 44 && stream->available()) got += stream->readBytes(header + got, 44 - got);
    uint8_t buf[512]; size_t written = 0;
    while (http.connected()) {
      int n = stream->readBytes(buf, sizeof(buf));
      if (n <= 0) break;
      i2s_write(I2S_NUM_1, buf, n, &written, portMAX_DELAY);
    }
    http.end(); return true;
  }
  Serial.println("[TTS] 该提供商暂未实现，请在 config.h 中选择 TTS_OPENAI 或 TTS_NONE");
  return false;
}

// LLM：根据提示生成故事文本；失败返回 false，调用方回落到本地模板
static bool generateStoryAI(const String& prompt, String& outStory) {
  if (LLM_PROVIDER == LLM_NONE || !wifiConnected) return false;
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http; http.setTimeout(HTTP_TIMEOUT_MS);
  http.begin(client, String(OPENAI_API_BASE) + "/chat/completions");
  http.addHeader("Authorization", String("Bearer ") + OPENAI_API_KEY);
  http.addHeader("Content-Type", "application/json");
  JsonDocument req;
  req["model"] = LLM_OPENAI_MODEL; req["max_tokens"] = LLM_MAX_TOKENS; req["temperature"] = LLM_TEMPERATURE;
  JsonArray msgs = req["messages"].to<JsonArray>();
  JsonObject sys = msgs.add<JsonObject>(); sys["role"] = "system";
  sys["content"] = "你是一位给 3-8 岁孩子讲睡前故事的讲故事人。用中文讲一个 150 字左右的温馨短故事，语言简单，有一个明确的小道理。";
  JsonObject usr = msgs.add<JsonObject>(); usr["role"] = "user"; usr["content"] = prompt;
  String payload; serializeJson(req, payload);
  int code = http.POST(payload);
  if (code != 200) { Serial.printf("[LLM] HTTP %d\n", code); http.end(); return false; }
  JsonDocument doc; deserializeJson(doc, http.getString());
  if (doc["choices"][0]["message"]["content"].is<const char*>()) {
    outStory = doc["choices"][0]["message"]["content"].as<String>(); http.end(); return outStory.length() > 0;
  }
  http.end(); return false;
}

// 录音：按住按钮期间采集，静音 1.5 秒或满 8 秒停止；返回采样数
static size_t recordAudio(int16_t* out, size_t maxCount) {
  size_t count = 0;
  unsigned long lastVoice = millis();
  int32_t raw[256]; size_t readBytes = 0;
  while (count < maxCount && millis() - lastVoice < SILENCE_TIMEOUT_MS) {
    if (i2s_read(I2S_NUM_0, raw, sizeof(raw), &readBytes, 100 / portTICK_PERIOD_MS) != ESP_OK) continue;
    int n = readBytes / 4;
    for (int i = 0; i < n && count < maxCount; i++) {
      int16_t s = (int16_t)(raw[i] >> 16); // 32-bit 槽取高 16 位
      out[count++] = s;
      if (abs(s) > VAD_THRESHOLD) lastVoice = millis();
    }
  }
  return count;
}

// ---------- 语音唤醒 ----------
// 状态机：IDLE（待机，常听唤醒词）-> AWAKE（已唤醒，等指令，10 秒超时）-> IDLE
// 唤醒词在 config.h 中配置，默认为不启用（WAKE_NONE），只用按钮。
enum WakeState { WAKE_IDLE, WAKE_AWAKE };
static WakeState wakeState = WAKE_IDLE;
static unsigned long awakeUntil = 0;
static unsigned long wakeCooldownUntil = 0;

// 本地 WakeNet 集成点：把训练好的“小可爱”模型放进固件后，在这里调用 ESP-SR 的
// wakenet 检测接口，返回 true 表示检测到唤醒词。未集成模型时返回 false。
static bool wakenetDetect(const int16_t* /*samples*/, size_t /*count*/) {
  // TODO：集成 ESP-SR WakeNet 模型后实现。模型训练见 firmware/README.md。
  return false;
}

// 云端验证：录 WAKE_LISTEN_SECONDS 秒，发 STT，看识别结果是否含唤醒词
static bool cloudVerifyWakeWord() {
  if (STT_PROVIDER == STT_NONE || !wifiConnected) return false;
  size_t maxCount = AUDIO_SAMPLE_RATE * WAKE_LISTEN_SECONDS;
  int16_t* samples = (int16_t*)ps_malloc(maxCount * sizeof(int16_t));
  if (!samples) return false;
  size_t count = 0;
  unsigned long start = millis();
  int32_t raw[256]; size_t readBytes = 0;
  while (count < maxCount && millis() - start < WAKE_LISTEN_SECONDS * 1000UL) {
    if (i2s_read(I2S_NUM_0, raw, sizeof(raw), &readBytes, 100 / portTICK_PERIOD_MS) != ESP_OK) continue;
    int n = readBytes / 4;
    for (int i = 0; i < n && count < maxCount; i++) samples[count++] = (int16_t)(raw[i] >> 16);
  }
  String heard;
  bool ok = count > 1000 && transcribeAudio(samples, count, heard) && heard.indexOf(WAKE_WORD) >= 0;
  if (ok) Serial.printf("[唤醒] 听到唤醒词，识别结果：%s\n", heard.c_str());
  free(samples);
  return ok;
}

static bool checkWakeWord() {
  if (WAKE_MODE == WAKE_NONE || millis() < wakeCooldownUntil) return false;
  if (WAKE_MODE == WAKE_CLOUD_VERIFY) return cloudVerifyWakeWord();
  if (WAKE_MODE == WAKE_LOCAL_WAKENET) {
    // 本地模式：读一小段音频交给 WakeNet 检测
    int16_t buf[1600]; int32_t raw[256]; size_t readBytes = 0; size_t count = 0;
    while (count < 1600) {
      if (i2s_read(I2S_NUM_0, raw, sizeof(raw), &readBytes, 100 / portTICK_PERIOD_MS) != ESP_OK) continue;
      int n = readBytes / 4;
      for (int i = 0; i < n && count < 1600; i++) buf[count++] = (int16_t)(raw[i] >> 16);
    }
    return wakenetDetect(buf, count);
  }
  return false;
}

static void onWake() {
  wakeState = WAKE_AWAKE;
  awakeUntil = millis() + WAKE_AWAKE_TIMEOUT_MS;
  wakeCooldownUntil = millis() + WAKE_COOLDOWN_MS;
  playTone(880, 120); playTone(1046, 180); // 唤醒提示音，和普通提示音不同
  lcd.fillScreen(TFT_WHITE);
  lcd.setTextColor(TFT_BLACK, TFT_WHITE);
  lcd.setTextSize(2);
  lcd.setCursor(8, 10);
  lcd.println("我在听");
  lcd.setTextSize(1);
  lcd.setCursor(8, 40);
  lcd.println("请说出你想要的故事，");
  lcd.println("比如：讲一个小猫去太空的故事");
  Serial.println("[唤醒] 已唤醒，等待指令，10 秒超时");
}

// ---------- LCD 显示 ----------
static void showStory(int idx) {
  const Story& s = STORIES[idx];
  lcd.fillScreen(TFT_WHITE);
  lcd.setTextColor(TFT_BLACK, TFT_WHITE);
  lcd.setTextSize(2);
  lcd.setCursor(8, 10);
  lcd.println("📖 故事机");
  lcd.setTextSize(2);
  lcd.setCursor(8, 40);
  lcd.println(s.title);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_DARKGREY, TFT_WHITE);
  lcd.setCursor(8, 70);
  lcd.printf("%s · %s · %s", s.hero, s.scene, s.lesson);
  lcd.setTextColor(TFT_BLACK, TFT_WHITE);
  lcd.setTextSize(2);
  lcd.setCursor(8, 95);
  // 简单换行：按字符数切，中文一个字占 2 个英文字符宽，LovyanGFX 的 println 会自动换行
  lcd.println(s.body);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_DARKGREY, TFT_WHITE);
  lcd.setCursor(8, 300);
  if (WAKE_MODE != WAKE_NONE) lcd.println("说“小可爱”唤醒我 · 按 BOOT 键换故事");
  else lcd.println("按 BOOT 键 = 下一个故事");
  Serial.printf("[故事] %s | %s | %s | %s\n", s.title, s.hero, s.scene, s.lesson);
  Serial.println(s.body);
}

// 语音指令流程（长按按钮和唤醒后共用）：录音 -> STT -> LLM 生成 -> TTS 播放
// 任一步失败回落到本地故事
static void processVoiceCommand() {
  if (STT_PROVIDER == STT_NONE) {
    Serial.println("[语音] STT 未启用（config.h 中 STT_PROVIDER=STT_NONE）");
    showStory(storyIndex);
    return;
  }
  size_t maxCount = AUDIO_SAMPLE_RATE * MAX_RECORD_SECONDS;
  int16_t* samples = (int16_t*)ps_malloc(maxCount * sizeof(int16_t));
  if (!samples) { showStory(storyIndex); return; }
  size_t count = recordAudio(samples, maxCount);
  Serial.printf("[语音] 录音 %u 个采样\n", (unsigned)count);
  String heard;
  if (count > 1000 && transcribeAudio(samples, count, heard)) {
    Serial.printf("[STT] 听到：%s\n", heard.c_str());
    String aiStory;
    String prompt = "孩子说：" + heard + "。请根据这句话编一个睡前故事。";
    if (generateStoryAI(prompt, aiStory)) {
      Serial.println("[LLM] 已生成故事，正在用 TTS 播放");
      if (!speakText(aiStory)) showStory(storyIndex);
    } else {
      Serial.println("[LLM] 生成失败，回落到本地故事");
      showStory(storyIndex);
    }
  } else {
    Serial.println("[STT] 识别失败，回落到本地故事");
    showStory(storyIndex);
  }
  free(samples);
  wakeState = WAKE_IDLE;
}

// ---------- setup / loop ----------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[故事机] 启动，H1-H5 v0.3");
  Serial.printf("[配置] STT=%d TTS=%d LLM=%d 唤醒=%d 唤醒词=%s 语言=%s\n", STT_PROVIDER, TTS_PROVIDER, LLM_PROVIDER, WAKE_MODE, WAKE_WORD, STORY_LANG);
  if (WAKE_MODE != WAKE_NONE) Serial.println("[唤醒] 已启用，常听模式：只有检测到声音后才会录音验证");

  // 背光：先开
  pinMode(PIN_LCD_BLK, OUTPUT);
  digitalWrite(PIN_LCD_BLK, HIGH);

  // H1: 屏幕
  lcd.init();
  lcd.setRotation(0);
  lcd.fillScreen(TFT_WHITE);
  lcd.setTextColor(TFT_BLACK, TFT_WHITE);
  lcd.setTextSize(2);
  lcd.setCursor(8, 10);
  lcd.println("📖 故事机");
  lcd.setTextSize(1);
  lcd.setCursor(8, 40);
  lcd.println("正在启动...");
  lcd.println("H1 LCD OK");
  Serial.println("[H1] LCD OK");

  // H2/H3: I2S
  i2sMicInit();
  i2sSpkInit();
  Serial.println("[H2/H3] I2S OK: Mic I2S0, Spk I2S1, 16kHz");

  pinMode(PIN_BTN, INPUT_PULLUP);

  // H5: 只有在 config.h 中启用了任一云端提供商时才联网；否则保持离线
  connectWiFi();

  // H2: 提示音，确认功放出声
  playChime();
  lcd.setCursor(8, 60);
  lcd.println("H2 功放 OK (听到提示音了吗?)");

  // H4: 第一个本地故事
  showStory(storyIndex);
  Serial.println("[H4] 本地故事就绪，按 BOOT 键换故事");
}

void loop() {
  // H3: 麦克风电平，每 500ms 读一次，串口打印，确认麦克风有信号
  static unsigned long lastMic = 0;
  static int lastMicLevel = 0;
  if (millis() - lastMic > 500) {
    lastMic = millis();
    int32_t buf[128];
    size_t readBytes = 0;
    if (i2s_read(I2S_NUM_0, buf, sizeof(buf), &readBytes, 100 / portTICK_PERIOD_MS) == ESP_OK && readBytes > 0) {
      int n = readBytes / 4;
      long sum = 0;
      for (int i = 0; i < n; i++) sum += abs(buf[i] >> 8);
      lastMicLevel = n ? (int)(sum / n / 1000) : 0;
      Serial.printf("[H3] Mic level=%d (对着麦克风说话，数字应该变大)\n", lastMicLevel);
    }
  }

  // 语音唤醒：待机时检测到声音（电平 > 5，按串口 Mic level 的实际值调），验证唤醒词
  if (WAKE_MODE != WAKE_NONE && wakeState == WAKE_IDLE && lastMicLevel > 5) {
    if (checkWakeWord()) onWake();
  }
  // 已唤醒：10 秒超时回待机；检测到说话声，自动处理语音指令
  if (wakeState == WAKE_AWAKE) {
    if (millis() > awakeUntil) {
      wakeState = WAKE_IDLE;
      Serial.println("[唤醒] 超时，回到待机");
      showStory(storyIndex);
    } else if (lastMicLevel > 5) {
      processVoiceCommand();
    }
  }

  // 按钮：短按（<800ms）= 下一个本地故事；长按（>=800ms）= 语音输入
  static int lastBtn = HIGH;
  static unsigned long pressStart = 0;
  static bool longPressHandled = false;
  int btn = digitalRead(PIN_BTN);
  if (btn == LOW && lastBtn == HIGH) { pressStart = millis(); longPressHandled = false; }
  if (btn == LOW && !longPressHandled && millis() - pressStart >= 800) {
    longPressHandled = true;
    Serial.println("[语音] 开始录音，松开按钮结束（最长 8 秒）");
    processVoiceCommand();
  }
  if (btn == HIGH && lastBtn == LOW && !longPressHandled && millis() - pressStart < 800 && millis() - pressStart > 50) {
    storyIndex = (storyIndex + 1) % STORY_COUNT;
    playChime();
    showStory(storyIndex);
  }
  lastBtn = btn;
  delay(10);
}
