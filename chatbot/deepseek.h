// deepseek.h - minimal DeepSeek (OpenAI-compatible) chat client for ESP32.
#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

struct DsReply {
  bool ok = false;
  int httpCode = 0;
  String text;       // assistant answer
  String reasoning;  // deepseek-reasoner "thinking", if any
  String finish;     // finish_reason, e.g. "stop"
  String error;      // human readable error, empty on success
};

class DeepSeekClient {
 public:
  void begin();
  void setApiKey(const String &k) { key_ = k; }
  void setSinks(void (*text)(const String &), void (*reason)(const String &)) {
    sinkText_ = text;
    sinkReason_ = reason;
  }

  // jsonBody is the complete {"model":...,"messages":[...]} request.
  DsReply chat(const String &jsonBody, bool stream);

 private:
  bool doStream(const String &body, DsReply &r);
  bool doOnce(const String &body, DsReply &r);
  String describeHttpError(int code, const String &payload) const;

  WiFiClientSecure tls_;
  HTTPClient http_;
  String key_;
  String url_;
  void (*sinkText_)(const String &) = nullptr;
  void (*sinkReason_)(const String &) = nullptr;
};
