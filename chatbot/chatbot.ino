// ============================================================================
//  DeepSeek ESP32-S3 Chatbot
// ----------------------------------------------------------------------------
//  A serial-terminal chatbot.  Configuration (Wi-Fi, API key, system role,
//  model, generation settings) is compiled in from info.txt at build time.
//
//  Flash the merged single binary at 0x0, open a serial terminal at 115200
//  baud and type.
// ============================================================================

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <time.h>
#include <vector>

#include "app_config.h"
#include "console.h"
#include "deepseek.h"

// ---------------------------------------------------------------- runtime ---
static String g_apiKey = CFG_API_KEY;
static String g_systemRole = CFG_SYSTEM_ROLE;
static String g_model = CFG_MODEL;
static int g_maxTokens = CFG_MAX_TOKENS;
static float g_temperature = CFG_TEMPERATURE;
static int g_historyChars = CFG_HISTORY_CHARS;
static bool g_stream = (CFG_STREAM != 0);

struct Turn {
  String role;
  String content;
};
static std::vector<Turn> g_history;

static Console con;
static DeepSeekClient ds;

static bool s_reasoningStarted = false;
static bool s_answerStarted = false;

// ---------------------------------------------------------------- helpers ---
static bool wifiConnected() { return WiFi.status() == WL_CONNECTED; }

static bool clockValid() { return time(nullptr) > 1700000000; }  // after 2023-11

static String maskKey(const String &k) {
  if (k.length() == 0) return "(not set)";
  if (k.length() <= 10) return "****";
  return k.substring(0, 5) + "..." + k.substring(k.length() - 4);
}

static String clockSummary() {
  if (!clockValid()) return "not synced";
  time_t now = time(nullptr);
  struct tm tmv;
  gmtime_r(&now, &tmv);
  char b[32];
  strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S UTC", &tmv);
  return String(b);
}

static String uptimeSummary() {
  unsigned long s = millis() / 1000UL;
  char b[40];
  snprintf(b, sizeof(b), "%luh %lum %lus", s / 3600UL, (s % 3600UL) / 60UL, s % 60UL);
  return String(b);
}

static String wifiSummary() {
  if (!wifiConnected()) return "disconnected";
  return WiFi.localIP().toString() + "  RSSI " + String(WiFi.RSSI()) + " dBm";
}

static void prompt() { con.write("> "); }

// ------------------------------------------------------------- streaming ----
static void onTextDelta(const String &s) {
  if (!s_answerStarted) {
    if (s_reasoningStarted) con.write("\n\n");
    s_answerStarted = true;
  }
  con.write(s);
}

static void onReasoningDelta(const String &s) {
  if (!s_reasoningStarted) {
    con.write("[reasoning] ");
    s_reasoningStarted = true;
  }
  con.write(s);
}

// ------------------------------------------------------------------- wifi ---
static bool connectWiFi(uint32_t timeoutMs) {
  if (wifiConnected()) return true;

  if (strlen(CFG_WIFI_SSID) == 0) {
    con.println("[wifi] WIFI_SSID is empty - set it in info.txt and rebuild.");
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.persistent(false);  // credentials live in the .bin, don't rewrite NVS each boot
  con.write(String("[wifi] connecting to \"") + CFG_WIFI_SSID + "\" ");
  WiFi.begin(CFG_WIFI_SSID, CFG_WIFI_PASS);

  uint32_t start = millis();
  while (!wifiConnected() && millis() - start < timeoutMs) {
    delay(300);
    con.write(".");
  }
  con.println();

  if (!wifiConnected()) {
    con.println(String("[wifi] failed (status ") + String(static_cast<int>(WiFi.status())) +
                "). Check that the network is 2.4 GHz and the password is right, then /wifi.");
    return false;
  }
  con.println("[wifi] connected: " + wifiSummary());
  return true;
}

static bool syncClock(uint32_t timeoutMs) {
  if (clockValid()) return true;
  con.write("[clock] syncing NTP ");
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com", "ntp.aliyun.com");
  uint32_t start = millis();
  while (!clockValid() && millis() - start < timeoutMs) {
    delay(250);
    con.write(".");
  }
  con.println();
  if (!clockValid()) {
    con.println("[clock] NTP failed. HTTPS needs a valid clock; try /clock later.");
    return false;
  }
  con.println("[clock] " + clockSummary());
  return true;
}

// --------------------------------------------------------------- history ----
static void trimHistory() {
  size_t total = 0;
  for (const Turn &t : g_history) total += t.content.length();
  while (g_history.size() > 2 &&
         (g_history.size() > 12 || total > static_cast<size_t>(g_historyChars))) {
    total -= g_history[0].content.length();
    total -= g_history[1].content.length();
    g_history.erase(g_history.begin());
    g_history.erase(g_history.begin());
  }
}

// ------------------------------------------------------------ request body --
static String buildBody(const String &userText, bool stream) {
  JsonDocument doc;
  doc["model"] = g_model;

  JsonArray msgs = doc["messages"].to<JsonArray>();
  if (g_systemRole.length() > 0) {
    JsonObject sys = msgs.add<JsonObject>();
    sys["role"] = "system";
    sys["content"] = g_systemRole;
  }
  for (const Turn &t : g_history) {
    JsonObject m = msgs.add<JsonObject>();
    m["role"] = t.role;
    m["content"] = t.content;
  }
  JsonObject u = msgs.add<JsonObject>();
  u["role"] = "user";
  u["content"] = userText;

  doc["stream"] = stream;
  doc["max_tokens"] = g_maxTokens;
  doc["temperature"] = g_temperature;

  String out;
  serializeJson(doc, out);
  return out;
}

// ------------------------------------------------------------------ chat ----
static void doChat(const String &text) {
  if (text.length() == 0) return;

  if (!wifiConnected()) {
    con.println("[wifi] not connected - reconnecting...");
    if (!connectWiFi(20000)) {
      con.println("[error] no Wi-Fi, message not sent.");
      return;
    }
  }
  if (!clockValid()) {
    syncClock(15000);
    if (!clockValid()) {
      con.println("[error] clock not set - cannot verify the TLS certificate. Try /clock.");
      return;
    }
  }

  const String body = buildBody(text, g_stream);
  con.println();
  con.write("DeepSeek: ");

  s_reasoningStarted = false;
  s_answerStarted = false;

  bool streamed = g_stream;
  uint32_t t0 = millis();
  DsReply r = ds.chat(body, streamed);
  uint32_t dt = millis() - t0;

  // If streaming produced nothing usable, retry once with a plain request.
  if (streamed && !r.ok && r.text.length() == 0) {
    con.println();
    con.println("[warn] no data from the stream, retrying without it ...");
    streamed = false;
    t0 = millis();
    r = ds.chat(body, false);
    dt = millis() - t0;
  }

  if (!streamed) {
    if (r.ok) {
      if (r.reasoning.length() > 0) {
        con.println("(thinking) " + r.reasoning + "\n");
      }
      con.println(r.text);
    }
  } else if (r.ok) {
    con.println();
  }

  if (!r.ok) {
    con.println();
    con.println("[error] " + r.error);
    return;
  }
  if (r.text.length() == 0) {
    con.println("[warn] empty answer from model.");
    return;
  }

  g_history.push_back({"user", text});
  g_history.push_back({"assistant", r.text});
  trimHistory();

  con.printf("[%lu ms, %u chars]\n", static_cast<unsigned long>(dt),
             static_cast<unsigned>(r.text.length()));
}

// -------------------------------------------------------------- commands ----
static void printHelp() {
  con.println("Commands:");
  con.println("  /help            show this help");
  con.println("  /new             start a new conversation (clear context)");
  con.println("  /system          show the system role");
  con.println("  /system <text>   change the system role (this session only)");
  con.println("  /model [name]    show or switch model (deepseek-chat, deepseek-reasoner)");
  con.println("  /temp <0-2>      set temperature");
  con.println("  /maxtok <n>      set max_tokens");
  con.println("  /stream on|off   toggle streaming output");
  con.println("  /key [sk-...]    show or set the API key (this session only)");
  con.println("  /info            show current settings");
  con.println("  /wifi            reconnect Wi-Fi");
  con.println("  /clock           re-sync the clock (NTP)");
  con.println("  /reboot          restart the board");
  con.println("Tip: start a message with // to send text beginning with '/'.");
}

static void printInfo() {
  con.println("--- settings ---");
  con.println("API key : " + maskKey(g_apiKey) + "   (compiled from info.txt)");
  con.println("Model   : " + g_model);
  con.println("System  : " + (g_systemRole.length() > 100
                               ? g_systemRole.substring(0, 100) + "..."
                               : g_systemRole));
  con.println("Temp    : " + String(g_temperature, 2) + "    Max tokens: " + String(g_maxTokens));
  con.println("Stream  : " + String(g_stream ? "on" : "off"));
  size_t chars = 0;
  for (const Turn &t : g_history) chars += t.content.length();
  con.println("History : " + String(static_cast<unsigned>(g_history.size())) + " messages, " +
              String(static_cast<unsigned>(chars)) + "/" + String(g_historyChars) + " chars");
  con.println("Wi-Fi   : " + wifiSummary());
  con.println("Clock   : " + clockSummary());
  con.println("Firmware: build " + String(CFG_BUILD_STAMP));
  con.println("Heap    : " + String(ESP.getFreeHeap() / 1024) + " KB free");
  con.println("Uptime  : " + uptimeSummary());
}

static void handleCommand(const String &line) {
  String name = line;
  String arg;
  int sp = line.indexOf(' ');
  if (sp >= 0) {
    name = line.substring(0, sp);
    arg = line.substring(sp + 1);
    arg.trim();
  }

  if (name == "/help") {
    printHelp();
  } else if (name == "/new") {
    g_history.clear();
    con.println("[ok] context cleared.");
  } else if (name == "/system") {
    if (arg.length() == 0) {
      con.println("System role: " + g_systemRole);
    } else {
      g_systemRole = arg;
      con.println("[ok] system role updated for this session.");
    }
  } else if (name == "/model") {
    if (arg.length() == 0) {
      con.println("Model: " + g_model + "   (available: deepseek-chat, deepseek-reasoner)");
    } else {
      g_model = arg;
      con.println("[ok] model = " + g_model);
    }
  } else if (name == "/temp") {
    if (arg.length() == 0) {
      con.println("Temperature: " + String(g_temperature, 2));
    } else {
      float v = arg.toFloat();
      if (v < 0.0f) v = 0.0f;
      if (v > 2.0f) v = 2.0f;
      g_temperature = v;
      con.println("[ok] temperature = " + String(g_temperature, 2));
    }
  } else if (name == "/maxtok") {
    if (arg.length() == 0) {
      con.println("max_tokens: " + String(g_maxTokens));
    } else {
      long v = arg.toInt();
      if (v < 16) v = 16;
      if (v > 8192) v = 8192;
      g_maxTokens = static_cast<int>(v);
      con.println("[ok] max_tokens = " + String(g_maxTokens));
    }
  } else if (name == "/stream") {
    if (arg == "on") {
      g_stream = true;
    } else if (arg == "off") {
      g_stream = false;
    }
    con.println(String("[ok] streaming ") + (g_stream ? "on" : "off"));
  } else if (name == "/key") {
    if (arg.length() == 0) {
      con.println("API key: " + maskKey(g_apiKey));
    } else {
      g_apiKey = arg;
      ds.setApiKey(g_apiKey);
      con.println("[ok] API key set for this session: " + maskKey(g_apiKey));
    }
  } else if (name == "/info") {
    printInfo();
  } else if (name == "/wifi") {
    connectWiFi(20000);
  } else if (name == "/clock") {
    syncClock(20000);
  } else if (name == "/reboot") {
    con.println("[reboot]");
    delay(200);
    ESP.restart();
  } else {
    con.println("[?] unknown command: " + name + "   (try /help)");
  }
}

static void handleLine(const String &raw) {
  String line = raw;
  line.trim();
  if (line.length() == 0) return;

  if (line.startsWith("//")) {  // escape for a literal leading slash
    doChat(line.substring(1));
  } else if (line.startsWith("/")) {
    handleCommand(line);
  } else {
    doChat(line);
  }
}

// ------------------------------------------------------------------- boot ----
static void printBanner() {
  con.println();
  con.println("============================================================");
  con.println("  DeepSeek ESP32-S3 Chatbot  v1.0");
  con.println("  build " CFG_BUILD_STAMP);
  con.println("============================================================");
  con.println("Model  : " + g_model + "    stream: " + (g_stream ? "on" : "off"));
  con.println("API key: " + maskKey(g_apiKey));
}

void setup() {
  con.begin();
  delay(600);
  setenv("TZ", "UTC0", 1);
  tzset();

  printBanner();

  if (g_apiKey.length() == 0 || g_apiKey.startsWith("sk-xxxx")) {
    con.println("[warn] API_KEY is not set in info.txt. Use /key sk-... for this session.");
  }
  if (g_model.length() == 0) g_model = "deepseek-chat";

  ds.begin();
  ds.setApiKey(g_apiKey);
  ds.setSinks(onTextDelta, onReasoningDelta);

  if (connectWiFi(30000)) {
    syncClock(20000);
  } else {
    con.println("[wifi] will keep retrying in the background.");
  }

  con.println();
  con.println("Type a message and press Enter.  /help for commands.");
  prompt();
}

void loop() {
  con.poll();
  if (con.available()) {
    const String line = con.takeLine();
    handleLine(line);
    prompt();
  }

  static uint32_t lastCheck = 0;
  static bool warnedDown = false;
  if (millis() - lastCheck > 5000) {
    lastCheck = millis();
    if (!wifiConnected()) {
      if (!warnedDown) {
        con.println("[wifi] disconnected - reconnecting...");
        warnedDown = true;
      }
      WiFi.reconnect();
    } else if (warnedDown) {
      con.println("[wifi] reconnected: " + wifiSummary());
      warnedDown = false;
      if (!clockValid()) syncClock(10000);
    }
  }

  delay(2);
}
