// app_config.h - compile-time configuration for the DeepSeek ESP32-S3 chatbot.
//
// The CFG_* values come from  generated_info.h  which is produced by
// tools/gen_info.py from  info.txt  at build time.  The fallbacks below let
// the sketch compile even if the generator was not run (e.g. Arduino IDE).
#pragma once

#include <Arduino.h>

#if __has_include("generated_info.h")
#include "generated_info.h"
#endif

#ifndef CFG_WIFI_SSID
#define CFG_WIFI_SSID ""
#endif
#ifndef CFG_WIFI_PASS
#define CFG_WIFI_PASS ""
#endif
#ifndef CFG_API_KEY
#define CFG_API_KEY ""
#endif
#ifndef CFG_SYSTEM_ROLE
#define CFG_SYSTEM_ROLE "You are a helpful assistant."
#endif
#ifndef CFG_MODEL
#define CFG_MODEL "deepseek-chat"
#endif
#ifndef CFG_MAX_TOKENS
#define CFG_MAX_TOKENS 512
#endif
#ifndef CFG_TEMPERATURE
#define CFG_TEMPERATURE 0.7f
#endif
#ifndef CFG_HISTORY_CHARS
#define CFG_HISTORY_CHARS 8000
#endif
#ifndef CFG_STREAM
#define CFG_STREAM 1
#endif
#ifndef CFG_BUILD_STAMP
#define CFG_BUILD_STAMP "dev"
#endif

// DeepSeek's OpenAI-compatible endpoint.
#ifndef CFG_BASE_URL
#define CFG_BASE_URL "https://api.deepseek.com/chat/completions"
#endif
