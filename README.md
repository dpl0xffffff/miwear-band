# miwear-band

GTK4 desktop companion for **Xiaomi Smart Band 9/10** on Linux, written in C.

Connect over Bluetooth Classic (RFCOMM), sync health and activity data, forward desktop notifications, and view a Mi Fitness–style dashboard.

## Build from source (quick guide)

### 1. Install dependencies

**Arch Linux** (package names):

```bash
sudo pacman -S --needed base-devel cmake pkg-config \
  gtk4 libadwaita glib2 json-glib libprotobuf-c sqlite bluez-libs openssl unzip
```

**Debian / Ubuntu** (approximate):

```bash
sudo apt install build-essential cmake pkg-config \
  libgtk-4-dev libadwaita-1-dev libglib2.0-dev libjson-glib-dev \
  libprotobuf-c-dev libsqlite3-dev libbluetooth-dev libssl-dev unzip
```

You need a working **BlueZ** stack and permission to use Bluetooth (often `bluetooth` group or Polkit).

### 2. Clone and configure

```bash
git clone https://github.com/notfatnotfunny/miwear-band.git
cd miwear-band
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

**Optional — sleep stage ML (TensorFlow Lite):**  
By default CMake tries to link TFLite for in-process sleep staging. If you do not have a TFLite build yet, either:

- Run the helper script (see `scripts/build-sleep-tflite.sh`), then reconfigure with  
  `-DMIWEAR_TFLITE_BUILD_DIR=/path/to/tflite-cmake`, or  
- Disable ML: `-DMIWEAR_ENABLE_SLEEP_ML=OFF` (embedded stage packets still work).

### 3. Compile

```bash
cmake --build build -j"$(nproc)"
```

The binary is `build/miwear-band`.

### 4. Run

```bash
./build/miwear-band
```

On first launch, pick the band in the pairing view and enter your **32-character Mi Fitness auth key** (hex).

### 5. Config (optional)

Settings are stored in `~/.config/miwear-band/config.ini`:

```ini
[device]
address=AA:BB:CC:DD:EE:FF
auth_key=0123456789abcdef0123456789abcdef
```

You can also set MAC and auth key in the UI before connecting.

---

## One-time band setup

1. Pair the band in **Mi Fitness** once and extract the **auth key** ([Gadgetbridge pairing docs](https://gadgetbridge.org/basics/pairing/huami-xiaomi-server/)).
2. Trust the device in BlueZ if needed: `bluetoothctl pair`, `trust`, `connect`.
3. Start **miwear-band** and connect.

## Debug

| Variable | Effect |
|----------|--------|
| `MIWEAR_VERBOSE_WIRE_LOG=1` | Hex RX + protobuf field dump |
| `MIWEAR_FETCH_DEBUG=1` | Activity fetch queue tracing |

## Project layout

| Path | Role |
|------|------|
| `src/spp/wire.c` | SPP framing, auth, protobuf dispatch |
| `src/spp/fitness_sync.c` | Mi Fitness–style sync |
| `src/ui/dashboard.c` | Health dashboard UI |
| `src/notifications/` | Desktop → band notifications |
| `src/sleep/` | Sleep stage pipeline (`docs/SLEEP_STAGES.md`) |
| `docs/MIFITNESS_SYNC.md` | APK-derived sync reference |

## License

This project is licensed under the **GNU General Public License v3.0**. See [LICENSE](LICENSE).

Third-party material under `third_party/` (Mi Fitness schemas, sleep models, reference parsers, etc.) remains under its respective upstream terms.
