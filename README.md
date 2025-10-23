# Insert Sound

A cross-platform utility that plays a random WAV sound upon USB device connection (skipping mass storage like flash drives on Windows). Runs in the background with a system tray icon on Windows for easy exit.

### Compilation

**Windows (MinGW):**
```bash
gcc usb_monitor.c -o usb_monitor.exe -luser32 -lwinmm -lsetupapi -mwindows
```

**Linux:**
```bash
gcc usb_monitor.c -o usb_monitor.out -ludev
```
Requires `libudev-dev` and `alsa-utils` (install via `sudo apt install libudev-dev alsa-utils` on Debian-based systems).

### Running
- Place one or more `.wav` files in the executable's directory.
- Run `./usb_monitor.out` (Linux) or `usb_monitor.exe` (Windows).
- On Windows, an icon appears in the system tray; right-click and select "Exit" to quit.
- Connect USB devices to test: non-storage devices trigger a random sound.

## Repository Structure

| File/Folder      | Purpose |
|------------------|---------|
| `usb_monitor.c`  | Main source code for USB monitoring and sound playback. |
| `README.md`      | This documentation. |
| (Optional) `.wav` files | Audio files for notifications (place in exe dir). |

## Usage
- The program monitors USB connections silently.
- Sounds play for devices like mice, keyboards, or headphones, flash drives.
- Randomly selects from all `.wav` files in the current directory (up to 100).
- On Linux, terminate with Ctrl+C or `kill`.
- On Windows, terminate via right click to tray icon
- No configuration needed; ensure WAV files are present for audio.

## Technical Notes
- Uses Windows API (DBT, SetupAPI) and libudev (Linux) for detection.
- Sound via `PlaySoundA` (Windows) or `aplay` (Linux).
- Efficient event-driven; no polling.
- Limitations: Detects all USB, including hubs; no device-specific filtering beyond storage.
