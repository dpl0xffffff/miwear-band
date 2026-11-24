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

On first launch, choose your device type on the **Add Device** page, pick it from the Bluetooth devices, enter what it needs to pair (for the Xiaomi band: the **32-character Mi Fitness auth key**) and press **Connect**. Afterwards the app opens on your saved data and connects from the banner or the Device page, where you can also add, switch and forget devices.

### 5. Config (optional)

Settings are stored in `~/.config/salus/config.ini` and health data in
`~/.local/share/salus/salus.db`. On first start, Salus copies the settings and data of the old
`miwear-band` app (`~/.config/miwear-band`, `~/.local/share/mi-band10-viewer`) if present:

```ini
[devices]
active=AA:BB:CC:DD:EE:FF

[device AA:BB:CC:DD:EE:FF]
driver=xiaomi-band
name=Xiaomi Smart Band 10
auth_key=0123456789abcdef0123456789abcdef
```

Devices are normally added from the UI.

---

## One-time setup: Xiaomi Smart Band

1. Pair the band in **Mi Fitness** once and extract the **auth key** ([Gadgetbridge pairing docs](https://gadgetbridge.org/basics/pairing/huami-xiaomi-server/)).
2. Trust the device in BlueZ if needed: `bluetoothctl pair`, `trust`, `connect`.
3. Start **Salus** and connect.

## Debug

| Variable | Effect |
|----------|--------|
| `SALUS_VERBOSE_WIRE_LOG=1` | Xiaomi: hex RX + protobuf field dump |
| `SALUS_FETCH_DEBUG=1` | Xiaomi: activity fetch queue tracing |

## Project layout

Salus has a device-independent core (UI, data store, services) and one driver per family of
devices. [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) describes the layers and how to add a
driver.

| Path | Role |
|------|------|
| `src/ui/` | libadwaita UI: Health (`dashboard.c`), `sleep_view.c`, `workouts_view.c`, `device_view.c`, Add Device flow (`connect.c`) |
| `src/core/` | App state, config and saved devices, active device (`device.c`) |
| `src/store/` | SQLite store and day timelines |
| `src/services/` | Weather, location, desktop notifications, find phone |
| `src/devices/` | Drivers: `xiaomi/` (Xiaomi Smart Band 9/10) |
| `docs/MIFITNESS_SYNC.md` | Xiaomi sync reference |

## License

This project is licensed under the **GNU General Public License v3.0**. See [LICENSE](LICENSE).

Third-party material under `third_party/` (Mi Fitness schemas, reference parsers, etc.) remains under its respective upstream terms.
