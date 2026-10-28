// config.h — 故事机 TTS / STT / 故事生成配置
// 本文件只放可公开的配置，不放任何密钥。
// 密钥、WiFi 密码一律放在 secrets.h（见 secrets.h.template），secrets.h 已加入 .gitignore，不提交。
#pragma once

// ---------- 提供商枚举（数值，不要改） ----------
enum { STT_NONE = 0, STT_OPENAI_WHISPER = 1, STT_AZURE = 2 };
enum { TTS_NONE = 0, TTS_OPENAI = 1, TTS_AZURE = 2 };
enum { LLM_NONE = 0, LLM_OPENAI = 1 };

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

// ---------- 网络 ----------
#define WIFI_TIMEOUT_MS 15000
#define HTTP_TIMEOUT_MS 30000
