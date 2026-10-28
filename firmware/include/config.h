// config.h — 故事机 TTS / STT / 故事生成配置
// 本文件只放可公开的配置，不放任何密钥。
// 密钥、WiFi 密码一律放在 secrets.h（见 secrets.h.template），secrets.h 已加入 .gitignore，不提交。
#pragma once

// ---------- 提供商枚举（数值，不要改） ----------
enum { STT_NONE = 0, STT_OPENAI_WHISPER = 1, STT_AZURE = 2 };
enum { TTS_NONE = 0, TTS_OPENAI = 1, TTS_AZURE = 2 };
enum { LLM_NONE = 0, LLM_OPENAI = 1 };
enum { WAKE_NONE = 0, WAKE_CLOUD_VERIFY = 1, WAKE_LOCAL_WAKENET = 2 };

// ---------- 语言与语音 ----------
#define STORY_LANG "zh-CN"          // 故事语言
#define TTS_VOICE "alloy"           // OpenAI TTS 语音名；Azure 时改用对应语音名，如 zh-CN-XiaoxiaoNeural
#define TTS_SPEED 1.0f              // 语速 0.5 - 2.0
#define TTS_VOLUME 80               // 音量 0 - 100，儿童使用建议不超过 80

// ---------- 音频参数（麦克风 / 功放共用） ----------
#define AUDIO_SAMPLE_RATE 16000     // 16 kHz，语音识别与合成的标准采样率
#define AUDIO_BITS 16               // 16-bit PCM，单声道
#define MIC_CHANNEL_LEFT true        // INMP441 L/R 接 GND = 左声道
#define VAD_THRESHOLD 800           // 语音活动检测阈值（16-bit PCM 幅值），越大越不容易误触发
#define MAX_RECORD_SECONDS 8        // 单次录音最长 8 秒
#define SILENCE_TIMEOUT_MS 1500     // 静音 1.5 秒即停止录音

// ---------- STT（语音转文字）提供商 ----------
// 可选值：STT_NONE（不启用语音输入，只用按钮）、STT_OPENAI_WHISPER、STT_AZURE
#define STT_PROVIDER STT_NONE
#define STT_OPENAI_MODEL "whisper-1"
#define STT_AZURE_REGION ""          // Azure 时填写，如 eastasia
#define STT_AZURE_LANGUAGE "zh-CN"

// ---------- TTS（文字转语音）提供商 ----------
// 可选值：TTS_NONE（不启用云端合成，只播提示音 + 屏幕显示文字）、TTS_OPENAI、TTS_AZURE
#define TTS_PROVIDER TTS_NONE
#define TTS_OPENAI_MODEL "tts-1"
#define TTS_OPENAI_FORMAT "wav"      // wav：直接取 PCM 播放，无需 MP3 解码
#define TTS_AZURE_VOICE "zh-CN-XiaoxiaoNeural"

// ---------- 故事生成（LLM）提供商 ----------
// 可选值：LLM_NONE（只用本地模板故事）、LLM_OPENAI
#define LLM_PROVIDER LLM_NONE
#define LLM_OPENAI_MODEL "gpt-4o-mini"
#define LLM_MAX_TOKENS 400
#define LLM_TEMPERATURE 0.8f

// ---------- 语音唤醒 ----------
#define WAKE_WORD "小可爱"           // 唤醒词
// 可选值：WAKE_NONE（默认，只用按钮，不常听）、WAKE_CLOUD_VERIFY、WAKE_LOCAL_WAKENET
// WAKE_CLOUD_VERIFY：麦克风常听，检测到声音后录 2 秒，发 STT 验证是否含唤醒词。
//   优点：今天就能用，不用训练模型；缺点：唤醒有 1-2 秒延迟，验证片段会上传到 STT 服务。
// WAKE_LOCAL_WAKENET：本地离线唤醒，见 firmware/README.md 的训练说明。
//   需要先用 Espressif ESP-SR 训练“小可爱”的 WakeNet 模型并放入固件，
//   未提供模型文件时自动回落到 WAKE_NONE，不会误报。
#define WAKE_MODE WAKE_NONE
#define WAKE_LISTEN_SECONDS 2       // 云端验证时每次录音时长
#define WAKE_VAD_THRESHOLD 1500     // 唤醒触发阈值，比普通 VAD 高，减少误触发
#define WAKE_AWAKE_TIMEOUT_MS 10000 // 唤醒后 10 秒内没说话，自动回到待机
#define WAKE_COOLDOWN_MS 3000       // 一次唤醒后 3 秒内不再重复触发

// ---------- 网络 ----------
#define WIFI_TIMEOUT_MS 15000
#define HTTP_TIMEOUT_MS 30000
