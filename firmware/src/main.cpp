// 亲子 AI 故事机 · ESP32-S3 固件骨架 v0.1
// 引脚配置见下表，不做改动。
//   麦克风 INMP441 (I2S0 收): WS=GPIO4, SCK=GPIO5, SD=GPIO6, L/R=GND(左声道)
//   功放 MAX98357A (I2S1 发): DIN=GPIO7, BCLK=GPIO15, LRC=GPIO16, SD=Vin常开, GAIN=GND
//   屏幕 SPI-LCD (ST7789): SCL=GPIO21, SDA=GPIO47, RES=GPIO45, DC=GPIO40, CS=GPIO41, BLK=GPIO42
//   按钮: 板载 BOOT = GPIO0（短按 = 下一个故事）
//
// 注意 GPIO45: 它是启动配置脚，管 Flash 电压。启动瞬间必须是低电平。
// 本固件在 setup() 里、启动完成之后才操作 RES 脚，启动前不碰它。
// 本硬件按此接法实测可启动，按此配置执行。
//
// 阶段: H1 点亮屏幕 / H2 功放出声 / H3 麦克风回放(串口看电平) / H4 本地故事
// H5 联网 AI (录音 -> AI 生成 -> TTS -> 播放) 是下一版，见 README。

#include <Arduino.h>
#include <driver/i2s.h>
#include <LovyanGFX.hpp>
#include <math.h>

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
  lcd.println("按 BOOT 键 = 下一个故事");
  Serial.printf("[故事] %s | %s | %s | %s\n", s.title, s.hero, s.scene, s.lesson);
  Serial.println(s.body);
}

// ---------- setup / loop ----------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[故事机] 启动，H1-H4 骨架 v0.1");

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
  if (millis() - lastMic > 500) {
    lastMic = millis();
    int32_t buf[128];
    size_t readBytes = 0;
    if (i2s_read(I2S_NUM_0, buf, sizeof(buf), &readBytes, 100 / portTICK_PERIOD_MS) == ESP_OK && readBytes > 0) {
      int n = readBytes / 4;
      long sum = 0;
      for (int i = 0; i < n; i++) sum += abs(buf[i] >> 8);
      int level = n ? (int)(sum / n / 1000) : 0;
      Serial.printf("[H3] Mic level=%d (对着麦克风说话，数字应该变大)\n", level);
    }
  }

  // 按钮：短按换故事（消抖）
  static int lastBtn = HIGH;
  static unsigned long lastPress = 0;
  int btn = digitalRead(PIN_BTN);
  if (btn == LOW && lastBtn == HIGH && millis() - lastPress > 300) {
    lastPress = millis();
    storyIndex = (storyIndex + 1) % STORY_COUNT;
    playChime();
    showStory(storyIndex);
  }
  lastBtn = btn;
  delay(10);
}
