// deepseek.cpp - HTTPS + Server-Sent-Events client for api.deepseek.com.
//
// Two transport modes are implemented:
//   * streaming  - the body is read incrementally and SSE "data:" events are
//                  parsed as they arrive, so tokens appear while generated.
//   * one-shot   - a single JSON response is read with HTTPClient::getString().
//
// The streaming reader is careful about HTTP/1.1 chunked transfer encoding:
// arduino-esp32's HTTPClient does NOT decode chunking on the raw stream, so we
// detect the first chunk-size line and decode the framing ourselves.  If the
// server instead answers with a plain (identity) body, we read it line by line.
#include "deepseek.h"

#include <ArduinoJson.h>
#include <NetworkClient.h>
#include <ctype.h>
#include <stdlib.h>

#include "app_config.h"
#include "certs.h"

namespace {

bool isHexLine(const String &s) {
  if (s.length() == 0 || s.length() > 8) return false;
  for (size_t i = 0; i < s.length(); i++) {
    if (!isxdigit(static_cast<unsigned char>(s[i]))) return false;
  }
  return true;
}

// Blocking read of one byte with a timeout. Returns -1 on timeout/close.
int readByte(NetworkClient *s, uint32_t timeoutMs) {
  uint32_t start = millis();
  for (;;) {
    if (s->available() > 0) return s->read();
    if (!s->connected()) return -1;
    if (millis() - start > timeoutMs) return -1;
    delay(1);
  }
}

// Reads one CRLF-terminated line (the CR is stripped). Returns false on
// timeout/close; `out` may still hold a partial line in that case.
bool readLine(NetworkClient *s, String &out, uint32_t timeoutMs) {
  out = "";
  uint32_t start = millis();
  for (;;) {
    while (s->available() > 0) {
      int c = s->read();
      if (c < 0) break;
      if (c == '\n') {
        if (out.endsWith("\r")) out.remove(out.length() - 1);
        return true;
      }
      out += static_cast<char>(c);
      if (out.length() > 16384) out = "";  // runaway garbage, resync
    }
    if (!s->connected() && s->available() == 0) return false;
    if (millis() - start > timeoutMs) return false;
    delay(1);
  }
}

// Incremental parser for "data: {json}" SSE events.
class SseParser {
 public:
  SseParser(DsReply &r, void (*text)(const String &), void (*reason)(const String &))
      : r_(r), sinkText_(text), sinkReason_(reason) {}

  void feed(char c) {
    if (done_) return;
    if (c == '\n') {
      if (line_.endsWith("\r")) line_.remove(line_.length() - 1);
      handleLine(line_);
      line_ = "";
    } else if (line_.length() < 16384) {
      line_ += c;
    }
  }

  void feedLine(const String &l) {
    if (done_) return;
    handleLine(l);
  }

  bool done() const { return done_; }

 private:
  void handleLine(const String &line) {
    if (!line.startsWith("data:")) return;  // ": ping", chunk headers, blanks
    String payload = line.substring(5);
    payload.trim();
    if (payload.length() == 0) return;
    if (payload == "[DONE]") {
      done_ = true;
      return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, payload) != DeserializationError::Ok) return;

    JsonVariant err = doc["error"];
    if (!err.isNull()) {
      const char *m = err["message"] | "unknown API error";
      r_.error = m;
      done_ = true;
      return;
    }

    JsonVariant choice = doc["choices"][0];
    const char *fin = choice["finish_reason"] | "";
    if (fin != nullptr && *fin) r_.finish = fin;

    const char *rc = choice["delta"]["reasoning_content"] | "";
    if (rc != nullptr && *rc) {
      r_.reasoning += rc;
      if (sinkReason_ != nullptr) sinkReason_(String(rc));
    }
    const char *c = choice["delta"]["content"] | "";
    if (c != nullptr && *c) {
      r_.text += c;
      if (sinkText_ != nullptr) sinkText_(String(c));
    }
  }

  DsReply &r_;
  void (*sinkText_)(const String &);
  void (*sinkReason_)(const String &);
  String line_;
  bool done_ = false;
};

}  // namespace

void DeepSeekClient::begin() {
  url_ = CFG_BASE_URL;
  tls_.setCACert(ROOT_CA_PEM);
  http_.setReuse(false);
  http_.setUserAgent("esp32s3-deepseek-chat/1.0");
}

DsReply DeepSeekClient::chat(const String &jsonBody, bool stream) {
  DsReply r;
  if (key_.length() == 0) {
    r.error = "API key is not set. Edit API_KEY in info.txt and rebuild, or use /key sk-...";
    return r;
  }
  if (stream) {
    doStream(jsonBody, r);
  } else {
    doOnce(jsonBody, r);
  }
  return r;
}

String DeepSeekClient::describeHttpError(int code, const String &payload) const {
  String detail;
  JsonDocument doc;
  if (deserializeJson(doc, payload) == DeserializationError::Ok) {
    const char *m = doc["error"]["message"] | "";
    if (m != nullptr && *m) detail = m;
  }
  if (detail.length() == 0) {
    String t = payload;
    t.replace("\r", " ");
    t.replace("\n", " ");
    if (t.length() > 160) t = t.substring(0, 160) + "...";
    detail = t;
  }

  String hint = "HTTP error";
  switch (code) {
    case 400: hint = "bad request"; break;
    case 401: hint = "API key rejected - check API_KEY in info.txt"; break;
    case 402: hint = "insufficient balance - top up your DeepSeek account"; break;
    case 422: hint = "invalid parameters"; break;
    case 429: hint = "rate limited - wait a few seconds"; break;
    case 500:
    case 502:
    case 503: hint = "DeepSeek server busy - retry shortly"; break;
    default: break;
  }
  return "HTTP " + String(code) + " (" + hint + "): " + detail;
}

bool DeepSeekClient::doOnce(const String &body, DsReply &r) {
  http_.begin(tls_, url_);
  http_.addHeader("Content-Type", "application/json");
  http_.addHeader("Authorization", "Bearer " + key_);
  http_.setConnectTimeout(15000);
  http_.setTimeout(60000);  // HTTPClient takes uint16_t milliseconds

  int code = http_.POST(body);
  if (code <= 0) {
    r.error = String("connection failed: ") + http_.errorToString(code);
    http_.end();
    return false;
  }
  r.httpCode = code;

  String payload = http_.getString();
  http_.end();

  if (code != 200) {
    r.error = describeHttpError(code, payload);
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    r.error = String("bad JSON from server: ") + err.c_str();
    return false;
  }
  const char *e = doc["error"]["message"] | "";
  if (e != nullptr && *e) {
    r.error = e;
    return false;
  }

  const char *c = doc["choices"][0]["message"]["content"] | "";
  if (c != nullptr) r.text = c;
  const char *rc = doc["choices"][0]["message"]["reasoning_content"] | "";
  if (rc != nullptr) r.reasoning = rc;
  const char *fin = doc["choices"][0]["finish_reason"] | "";
  if (fin != nullptr) r.finish = fin;

  r.ok = r.error.length() == 0 && (r.text.length() > 0 || r.finish == "stop");
  return r.ok;
}

bool DeepSeekClient::doStream(const String &body, DsReply &r) {
  http_.begin(tls_, url_);
  http_.addHeader("Content-Type", "application/json");
  http_.addHeader("Accept", "text/event-stream");
  http_.addHeader("Authorization", "Bearer " + key_);
  http_.setConnectTimeout(15000);
  http_.setTimeout(30000);

  int code = http_.POST(body);
  if (code <= 0) {
    r.error = String("connection failed: ") + http_.errorToString(code);
    http_.end();
    return false;
  }
  r.httpCode = code;

  if (code != 200) {
    String payload = http_.getString();
    http_.end();
    r.error = describeHttpError(code, payload);
    return false;
  }

  auto *stream = http_.getStreamPtr();
  if (stream == nullptr) {
    http_.end();
    r.error = "no response stream";
    return false;
  }

  SseParser parser(r, sinkText_, sinkReason_);

  String first;
  if (!readLine(stream, first, 30000)) {
    http_.end();
    r.error = "no response from server";
    return false;
  }
  while (first.length() == 0) {
    if (!readLine(stream, first, 30000)) {
      http_.end();
      r.error = "empty response from server";
      return false;
    }
  }

  if (isHexLine(first)) {
    // HTTP/1.1 chunked transfer encoding.
    uint32_t chunk = static_cast<uint32_t>(strtoul(first.c_str(), nullptr, 16));
    const uint32_t deadline = millis() + 180000;
    while (chunk > 0 && !parser.done()) {
      for (uint32_t i = 0; i < chunk && !parser.done(); i++) {
        int c = readByte(stream, 30000);
        if (c < 0) {
          http_.end();
          r.error = "stream stalled";
          return false;
        }
        parser.feed(static_cast<char>(c));
      }
      if (parser.done()) break;
      readByte(stream, 5000);  // CR
      readByte(stream, 5000);  // LF
      String sizeLine;
      if (!readLine(stream, sizeLine, 30000)) break;
      chunk = static_cast<uint32_t>(strtoul(sizeLine.c_str(), nullptr, 16));
      if (static_cast<int32_t>(millis() - deadline) >= 0) {
        r.error = "stream timeout";
        break;
      }
    }
  } else if (first.startsWith("{")) {
    // Server ignored "stream": it sent one plain JSON object.
    String json = first;
    while (json.length() < 65536) {
      String more;
      if (!readLine(stream, more, 30000)) break;
      if (more.length() == 0) break;
      json += more;
    }
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) {
      r.error = "unexpected response from server";
    } else {
      const char *e = doc["error"]["message"] | "";
      if (e != nullptr && *e) {
        r.error = e;
      } else {
        const char *c = doc["choices"][0]["message"]["content"] | "";
        if (c != nullptr) r.text = c;
        const char *rc = doc["choices"][0]["message"]["reasoning_content"] | "";
        if (rc != nullptr) r.reasoning = rc;
      }
    }
  } else {
    // Identity encoding: read SSE lines until [DONE] or close.
    parser.feedLine(first);
    while (!parser.done()) {
      String line;
      if (readLine(stream, line, 60000)) {
        parser.feedLine(line);
      } else {
        if (line.length() > 0) parser.feedLine(line);
        break;
      }
    }
  }

  http_.end();
  r.ok = r.error.length() == 0 && (r.text.length() > 0 || r.finish == "stop");
  return r.ok;
}
