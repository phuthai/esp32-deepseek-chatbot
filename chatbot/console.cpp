#include "console.h"

#include <stdarg.h>

void Console::begin() {
  // Both are opened unconditionally: when USB CDC is enabled Serial is the
  // native USB port and Serial0 is UART0; otherwise they are the same UART.
  Serial.begin(115200);
  Serial0.begin(115200);
}

void Console::write(const String &s) {
  Serial.write(reinterpret_cast<const uint8_t *>(s.c_str()), s.length());
#if CONSOLE_HAS_USB_CDC
  Serial0.write(reinterpret_cast<const uint8_t *>(s.c_str()), s.length());
#endif
}

void Console::println(const String &s) {
  write(s);
  write("\r\n");
}

void Console::printf(const char *fmt, ...) {
  char tmp[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  write(String(tmp));
}

void Console::echo(uint8_t c, bool toUsb) {
#if CONSOLE_HAS_USB_CDC
  if (toUsb) {
    Serial.write(&c, 1);
    return;
  }
#endif
  Serial0.write(&c, 1);
}

void Console::poll() {
  if (haveLine_) return;  // keep the pending line until it is consumed

  while (Serial.available() > 0) {
    int c = Serial.read();
    if (c < 0) break;
    feed(static_cast<uint8_t>(c), true);
    if (haveLine_) return;
  }
#if CONSOLE_HAS_USB_CDC
  while (Serial0.available() > 0) {
    int c = Serial0.read();
    if (c < 0) break;
    feed(static_cast<uint8_t>(c), false);
    if (haveLine_) return;
  }
#endif
}

void Console::feed(uint8_t c, bool fromUsb) {
  // Swallow the LF of a CRLF pair produced by the terminal.
  if (swallowLf_) {
    swallowLf_ = false;
    if (c == '\n') return;
  }

  if (c == '\r') {
    echo('\r', fromUsb);
    echo('\n', fromUsb);
    line_ = String(buf_, len_);
    len_ = 0;
    haveLine_ = true;
    swallowLf_ = true;
    return;
  }
  if (c == '\n') {
    echo('\r', fromUsb);
    echo('\n', fromUsb);
    line_ = String(buf_, len_);
    len_ = 0;
    haveLine_ = true;
    return;
  }

  if (c == '\b' || c == 0x7f) {
    if (len_ == 0) return;
    // Remove a whole UTF-8 code point, not just one byte.
    size_t n = 1;
    while (len_ - n > 0 && (buf_[len_ - n] & 0xC0) == 0x80) n++;
    len_ -= n;
    for (size_t i = 0; i < n; i++) {
      echo('\b', fromUsb);
      echo(' ', fromUsb);
      echo('\b', fromUsb);
    }
    return;
  }

  if (c < 0x20) return;  // ignore other control characters

  if (len_ >= CONSOLE_LINE_MAX - 1) return;  // line too long: drop
  buf_[len_++] = static_cast<char>(c);
  echo(c, fromUsb);
}

String Console::takeLine() {
  String out = line_;
  line_ = "";
  haveLine_ = false;
  return out;
}
