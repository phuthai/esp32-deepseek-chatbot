# DeepSeek ESP32-S3 Chatbot

A serial-terminal chatbot firmware for **ESP32-S3** that talks to the
**DeepSeek API** over HTTPS. Wi-Fi credentials, the API key, the system role and
the model are read from `info.txt` at build time and compiled into a **single
`.bin`** you flash at address `0x0`.

```
+-----------------+   USB/UART 115200    +--------------------------+
| serial terminal | <------------------> | ESP32-S3                 |
| (PuTTY, etc.)   |                      |  WiFi + TLS -> DeepSeek  |
+-----------------+                      +--------------------------+
```

## Quick start

```bat
git clone https://github.com/phuthai/esp32-deepseek-chatbot.git
cd esp32-deepseek-chatbot

:: 1. create your private config from the template
copy info.txt.example info.txt

:: 2. edit info.txt (Wi-Fi, API key, system role, model)
notepad info.txt

:: 3. build -> dist\DeepSeekChat_ESP32S3_4MB.bin
build.bat
```

Then flash `dist\DeepSeekChat_ESP32S3_4MB.bin` at **0x0**
(`flash.bat COM5` does that for you).

Requirements: Windows, Python 3.9+ (for the helper scripts), and the toolchain
(`python tools\setup_toolchain.py`, ~2 GB, one time).

## What you get

| File | Purpose |
| --- | --- |
| `dist\DeepSeekChat_ESP32S3_4MB.bin` | complete 4 MB image: bootloader + partition table + app |
| `dist\DeepSeekChat_ESP32S3_4MB.bin.sha256` | checksum |
| `info.txt` | the only file you normally edit |
| `build.bat` / `build.ps1` | regenerate config, compile, merge |
| `flash.bat` | `flash.bat COM5` writes the image at 0x0 |
| `chatbot/` | firmware sources |
| `tools/build.py` | the actual build pipeline (called by build.bat) |
| `tools/` | helper scripts (config generator, merge, certs, toolchain setup) |

The 4 MB image also boots on 8 MB / 16 MB boards (only the first 4 MB is used).

## info.txt

```ini
WIFI_SSID=MyHomeWiFi          # 2.4 GHz only - the S3 has no 5 GHz radio
WIFI_PASS=secret
API_KEY=sk-...                # https://platform.deepseek.com/api_keys
SYSTEM_ROLE=You are a helpful assistant. Answer in the same language as the user.
MODEL=deepseek-chat           # or deepseek-reasoner
MAX_TOKENS=512
TEMPERATURE=0.7
HISTORY_CHARS=8000            # 0 = no memory between turns
STREAM=1                      # 1 = tokens appear as they are generated
```

* UTF-8 is fine - Chinese system roles work. GBK-encoded files are converted
  automatically.
* `\n` inside `SYSTEM_ROLE` becomes a real line break.
* Everything is compiled in: **rebuild after every edit**. There is no
  provisioning portal or NVS setup screen by design.

## Flashing

### Option A - flash.bat (uses the bundled esptool)

```bat
flash.bat COM5
```

`flash.bat` with no arguments lists the detected serial ports.

### Option A2 - esptool (any platform)

```bash
pip install esptool
esptool.py --chip esp32s3 --port COM5 -b 460800 write_flash 0x0 DeepSeekChat_ESP32S3_4MB.bin
```

### Option B - Espressif Flash Download Tool (Windows GUI)

1. Chip type **ESP32-S3**, WorkMode **Develop**, load the `.bin`.
2. Set the offset to **0x0** and the flash size to **4MB** (or your real size).
3. Start. If it cannot connect, hold **BOOT**, tap **RST**, release BOOT.

### Option C - arduino-cli (toolchain already in this folder)

```bat
tools\arduino-cli.exe --config-file arduino-cli.yaml upload ^
  --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc,PartitionScheme=huge_app,FlashSize=4M ^
  --input-dir build\out --port COM5 chatbot
```

## Talking to it

Open the serial port at **115200 8N1** (either the native USB port or the
UART bridge, both are mirrored). The board prints a banner, connects to Wi-Fi,
syncs NTP (needed for TLS certificate checks) and shows `> `.

Type a message, press Enter. The answer streams in as it is generated.

### Commands

| Command | Meaning |
| --- | --- |
| `/help` | list commands |
| `/new` | clear the conversation context |
| `/system` / `/system <text>` | show / temporarily change the system role |
| `/model` / `/model <name>` | show / switch model for this session |
| `/temp <0-2>` | temperature |
| `/maxtok <n>` | `max_tokens` |
| `/stream on\|off` | streaming on/off |
| `/key` / `/key sk-...` | show / set API key for this session |
| `/info` | settings, IP, clock, heap, uptime |
| `/wifi` | reconnect Wi-Fi |
| `/clock` | re-sync NTP |
| `/reboot` | restart |

Runtime changes (`/system`, `/key`, `/model`, ...) last until reset. To make
them permanent, edit `info.txt` and rebuild.

A message starting with `//` is sent literally (so `//help` asks the model
about "/help").

## Rebuilding

```bat
build.bat
```

`build.bat` runs `tools\build.py`, which does three things:

1. `tools\gen_info.py` - parses `info.txt`, writes `chatbot\generated_info.h`.
2. `arduino-cli compile` - FQBN
   `esp32:esp32:esp32s3:CDCOnBoot=cdc,PartitionScheme=huge_app,FlashSize=4M`.
3. `tools\merge_bin.py` - writes the 4 MB image at offsets
   `0x0000` bootloader, `0x8000` partition table, `0x10000` app.

### Long folder paths

The ESP32 toolchain uses paths longer than the classic Windows 260-character
limit when the project sits in a deep folder (for example inside OneDrive).
The usual symptom is:

```
fatal error: bits/c++config.h: No such file or directory
```

`tools\build.py` handles this automatically: if the project path is long it
maps a free drive letter (`subst R: ...`), builds through the short path and
removes the mapping afterwards. If you prefer, move the folder somewhere short
like `C:\esp32-chatbot`.

### Moving the project to another PC

```bat
python tools\setup_toolchain.py   :: downloads arduino-cli + esp32 core + ArduinoJson
build.bat
```

The toolchain lives in `tools\` and `arduino-data\` inside this folder.

## Troubleshooting

| Symptom | Fix |
| --- | --- |
| `[wifi] failed (status 1)` | Wrong SSID/password, or the AP is 5 GHz-only. ESP32-S3 is 2.4 GHz only. |
| `[error] clock not set` | NTP blocked. Allow UDP 123, or set a router that answers NTP; `/clock` retries. |
| `HTTP 401` | Bad API key. Check `info.txt`, rebuild (or `/key sk-...`). |
| `HTTP 402` | DeepSeek account has no balance. |
| `HTTP 429` | Rate limited; wait a few seconds. |
| `connection failed: -1` | No route to `api.deepseek.com`; check DNS/proxy/firewall. |
| Garbage characters | Wrong baud rate - use 115200. |
| No COM port | Install the USB-serial driver (CP210x/CH340) or use the native USB port. |
| Board not booting after flash | Flash at 0x0, flash size 4 MB, and use the full merged bin (not the app-only one). |
| `bits/c++config.h: No such file` | Project path too long. Use `build.bat` (it maps a short drive automatically) or move the folder to e.g. `C:\esp32-chatbot`. |

## Repository / secrets

`info.txt`, `chatbot/generated_info.h` (compiled from it), `dist/` and the
8 GB toolchain in `arduino-data/` are **git-ignored**. The repo commits
`info.txt.example` only, so your API key and Wi-Fi password never reach GitHub.

## Notes

* The firmware uses `huge_app` (no OTA) so the app has ~3 MB of room; the
  resulting image is about 1.1 MB.
* TLS uses mbedTLS with Amazon Root CA 1 + Starfield Services Root CA G2, the
  roots behind `api.deepseek.com` (Amazon CloudFront). Certificates are
  validated - no `setInsecure()`.
* Streaming handles HTTP/1.1 chunked transfer encoding explicitly, because
  arduino-esp32's `HTTPClient` does not decode chunking on the raw stream.
* Conversation history is kept in RAM, capped by `HISTORY_CHARS` and 12
  messages (6 turns), because there is no PSRAM requirement.
* **The API key is inside the .bin.** Anyone with the binary can extract it.
  Treat the file as a secret; prefer a key with a spending limit.
