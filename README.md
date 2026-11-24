# Salus

Health app for smart bands and watches on Linux (GTK4/libadwaita, written in C). Connect your
device, sync measurements, sleep and workouts, forward desktop notifications, and browse your
history offline.

Devices are supported through drivers. Currently supported: **Xiaomi Smart Band 9/10**
(Bluetooth Classic RFCOMM, Mi Fitness protocol).

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

### 3. Compile

```bash
cmake --build build -j"$(nproc)"
```

The binary is `build/salus`. Run the unit tests with `ctest --test-dir build`.

### 4. Run

```bash
./build/salus
```

On first launch, pick the band on the setup page, enter your **32-character Mi Fitness auth key** (hex) and press **Connect**. Afterwards the app opens on your saved data and connects from the banner or the Device page.

### 5. Config (optional)

Settings are stored in `~/.config/salus/config.ini` and health data in
`~/.local/share/salus/salus.db`. On first start, Salus copies the settings and data of the old
`miwear-band` app (`~/.config/miwear-band`, `~/.local/share/mi-band10-viewer`) if present:

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
3. Start **Salus** and connect.

## Debug

| Variable | Effect |
|----------|--------|
| `SALUS_VERBOSE_WIRE_LOG=1` | Hex RX + protobuf field dump |
| `SALUS_FETCH_DEBUG=1` | Activity fetch queue tracing |

## Project layout

| Path | Role |
|------|------|
| `src/spp/wire.c` | SPP framing, auth, protobuf dispatch |
| `src/spp/fitness_sync.c` | Mi Fitness–style sync |
| `src/ui/` | libadwaita UI: shell + Health (`dashboard.c`), `sleep_view.c`, `workouts_view.c`, `device_view.c`, setup (`ui.c`) |
| `src/notifications/` | Desktop → band notifications |
| `src/sleep/` | Sleep stage pipeline (`docs/SLEEP_STAGES.md`) |
| `docs/MIFITNESS_SYNC.md` | APK-derived sync reference |

## License

This project is licensed under the **GNU General Public License v3.0**. See [LICENSE](LICENSE).

Third-party material under `third_party/` (Mi Fitness schemas, reference parsers, etc.) remains under its respective upstream terms.
