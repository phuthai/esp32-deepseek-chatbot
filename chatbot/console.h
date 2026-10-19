// console.h - line input / output over both the native USB-CDC port and UART0.
//
// Works whether the sketch was built with "USB CDC On Boot" enabled or not:
// both Serial and Serial0 are polled, and output is mirrored to both ports.
#pragma once

#include <Arduino.h>

#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
#define CONSOLE_HAS_USB_CDC 1
#else
#define CONSOLE_HAS_USB_CDC 0
#endif

#define CONSOLE_LINE_MAX 1024

class Console {
 public:
  void begin();

  // Poll both serial ports. Call often from loop().
  void poll();

  // True when a complete line has been received.
  bool available() const { return haveLine_; }

  // Returns the pending line (without CR/LF) and clears the flag.
  String takeLine();

  // Output helpers - mirrored to USB CDC and UART0.
  void write(const String &s);
  void print(const String &s) { write(s); }
  void println(const String &s);
  void println() { write("\r\n"); }
  void printf(const char *fmt, ...);

 private:
  void feed(uint8_t c, bool fromUsb);
  static void echo(uint8_t c, bool toUsb);

  char buf_[CONSOLE_LINE_MAX];
  size_t len_ = 0;
  bool haveLine_ = false;
  bool swallowLf_ = false;
  String line_;
};
