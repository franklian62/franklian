# GD32VW553 + GD32H759 Voice Warning Handoff

Last verified: 2026-06-19

This branch is a handoff package for the VW553 voice-warning demo. The current firmware can play the local prompt "禁止进入危险区域" through the AI audio daughter board. It is intended to be extended so a GD32H759I-START board sends a danger signal to the VW553 over UART.

## Current Status

- Board under test: GD32VW553K-START with AI audio daughter board / ES8311 codec.
- Voice path: ES8311 codec + I2S output is enabled.
- Prompt: local PCM16 prompt, 48 kHz, phrase "禁止进入危险区域".
- Playback behavior: firmware auto-plays once after boot. PC8 / BOOT0 active-low can also be used as a temporary danger-signal simulator.
- Build and flash: `Build ALL` and `Download ALL (JLink)` have been verified locally.
- Latest local build result: flash usage about 29.94%; JLink flash verify passed.

Important: the real H759-to-VW553 UART command receiver is not implemented yet. The current signal simulator is PC8 active-low. The next integration step is to add a VW553 UART RX task and call `voice_prompt_player_trigger()` when the H759 sends a danger command.

## Main Files Added Or Changed

- `MSDK/app/voice_app.c/.h`: voice demo startup.
- `MSDK/app/voice_codec_es8311.c/.h`: ES8311 soft-I2C init and DAC setup.
- `MSDK/app/voice_i2s_out.c/.h`: ring-buffered 48 kHz I2S output.
- `MSDK/app/voice_prompt_player.c/.h`: prompt playback task and trigger API.
- `MSDK/app/voice_prompt_danger.c/.h`: embedded PCM voice prompt.
- `MSDK/app/voice_danger_key_sim.c/.h`: temporary PC8 active-low trigger.
- `MSDK/app/app_cfg.h`: enables `CONFIG_VOICE_DEMO`, `CONFIG_VOICE_AI_AUDIO_BOARD`, and prompt/key simulator options.
- `MSDK/app/main.c`: starts the voice app from `application_init()`.
- `MSDK/plf/src/spi_i2s/spi_i2s.c`: I2S pin/timer configuration for the AI audio daughter board.
- `MSDK/plf/src/uart/uart_config.h` and `MSDK/plf/src/uart/log_uart.c`: log UART adjusted for the voice demo.
- `.vscode/tasks.json`: build, download, run, serial-monitor, and UART-download tasks.
- `.vscode/download.bat`: unified JLink/GDLink flashing script.

## Build And Flash

Open this folder in VS Code:

```text
GD32VW55x_WiFi_BLE_SDK
```

Recommended VS Code tasks:

1. `Build ALL`
2. `Download ALL (JLink)`
3. `Run Program (JLink)`

Equivalent PowerShell commands:

```powershell
.\cmake_build.bat app
.\.vscode\download.bat ALL JLink
.\tools\xpack-openocd-0.11.0-3_windows\bin\openocd.exe -f .\MSDK\projects\cmake\output\openocd_jlink.cfg -c "init; reset run; shutdown"
```

### Toolchain Notes For A Fresh Clone

This SDK does not track the extracted tool directories. A fresh clone may not contain:

```text
tools/gd32vw55x_toolchain_windows/bin/riscv-nuclei-elf-gcc.exe
tools/xpack-openocd-0.11.0-3_windows/bin/openocd.exe
```

For Windows, keep these compressed packages under `tools/`:

```text
tools/gd32vw55x_toolchain_windows.7z.001
tools/gd32vw55x_toolchain_windows.7z.002
tools/gd32vw55x_toolchain_windows.7z.003
tools/xpack-openocd-0.11.0-3_windows.7z
```

Then install 7-Zip and run:

```powershell
.\cmake_build.bat app
```

The script will auto-extract the Windows toolchain/OpenOCD packages into `tools/`.
If 7-Zip is not installed at `C:\Program Files\7-Zip\7z.exe`, either install it there or manually extract:

```text
tools/gd32vw55x_toolchain_windows.7z.001 -> tools/gd32vw55x_toolchain_windows/
tools/xpack-openocd-0.11.0-3_windows.7z -> tools/xpack-openocd-0.11.0-3_windows/
```

For Linux, keep the `tools/gd32vw55x_toolchain_linux.tar.gz00` through `tar.gz06` split files and `tools/xpack-openocd-0.11.0-3_linux.tar.gz`; `cmake_build.sh` will concatenate/extract them when needed.

## Voice Test

Expected behavior after reset:

1. The firmware starts the ES8311/I2S voice path.
2. It waits briefly for the codec/audio path to settle.
3. It plays "禁止进入危险区域".

Temporary button/signal simulation:

- PC8 / BOOT0 is configured as an active-low input in the current demo.
- Pull PC8 low after boot to trigger another playback.
- Do not use SW2 / `UartDownload` / PA15 as an application key. It is tied to download/JTAG-related behavior and previously caused debug connection problems.

## H759 To VW553 UART Plan

The H759 side PA2/PA3 plan is valid for the H759 board:

| H759 signal | H759 pin | Meaning |
| --- | --- | --- |
| USART1_TX | PA2 | H759 transmits danger command |
| USART1_RX | PA3 | H759 receives optional response |
| GND | GND | Common ground |

Recommended wiring for the next integration:

| H759 side | VW553 side |
| --- | --- |
| H759 PA2 / USART1_TX | VW553 UART RX pin selected by the VW553 firmware |
| H759 PA3 / USART1_RX | VW553 UART TX pin selected by the VW553 firmware |
| H759 GND | VW553 GND |

Use 3.3 V logic. Do not use 5 V UART levels.

For this audio build, avoid these VW553 pins for the H759 UART unless the pinmux is redesigned:

- VW553 PA2, PA1, PA12, PB1: currently used by I2S/audio timing/data.
- VW553 PB15/PA8: may be used by the ES8311 soft-I2C path on the AI audio board.
- VW553 PA15/SW2/`UartDownload`: do not use as a normal app key.

Practical recommendation for the next engineer:

- Use VW553 UART2 pins PA6/PA7 for the H759 link if accessible on the board/header.
- Current firmware also uses UART2 as the log UART in voice mode, so either:
  - reuse UART2 with a simple command parser and reduce log output, or
  - move logs elsewhere and dedicate UART2 to the H759 command link, or
  - select another unused VW553 UART pinmux after checking the schematic.

Suggested first command protocol:

```text
Baud: 115200
Format: 8 data bits, no parity, 1 stop bit
Command: DANGER\n
Action: VW553 calls voice_prompt_player_trigger()
```

## Suggested VW553 UART Receiver Work

Add a small receiver module, for example:

```text
MSDK/app/voice_danger_uart.c
MSDK/app/voice_danger_uart.h
```

Expected behavior:

1. Configure the selected VW553 UART at 115200 8N1.
2. Accumulate RX bytes until newline.
3. If the line is `DANGER`, call `voice_prompt_player_trigger()`.
4. Optionally reply `OK\r\n` to H759.

The current key simulator already shows the trigger side:

```c
voice_prompt_player_trigger();
```

or, for immediate blocking playback from an app task:

```c
voice_prompt_player_play_once();
```

Prefer `voice_prompt_player_trigger()` from interrupt or UART receive paths.

## H759 Firmware Work

On the H759 side:

1. Configure USART1 on PA2/PA3 at 115200 8N1.
2. When the danger condition is detected, send:

```c
"DANGER\n"
```

3. Optional: wait for `OK\r\n` from VW553.

## Known Notes

- The prompt source is embedded as PCM in `MSDK/app/voice_prompt_danger.c`; no external audio file is required at runtime.
- ES8311 DAC volume is intentionally not full-scale to avoid small-speaker distortion.
- The prompt contains leading silence so the first Chinese character is not clipped during codec startup.
- If the first character is clipped again, increase `VOICE_PROMPT_STARTUP_DELAY_MS` in `MSDK/app/voice_prompt_player.c` or regenerate the PCM prompt with a longer leading silence.
- If speech clarity is not sufficient, use a cleaner TTS source and regenerate `voice_prompt_danger.c`; the current I2S/codec path is already working.
