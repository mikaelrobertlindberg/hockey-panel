# 🏒 Hockey Panel

ESP32-S3 baserad display för SHL och HockeyAllsvenskan statistik med touch-interface.

## Hårdvara

- **Display:** Waveshare ESP32-S3-Touch-LCD-4.3 (SKU: DIS06043H)
- **Upplösning:** 800x480 IPS
- **Touch:** GT911 kapacitiv (5-punkt)
- **CPU:** ESP32-S3 Dual-core 240MHz
- **RAM:** 8MB PSRAM
- **Flash:** 16MB
- **Anslutning:** USB-C eller OTA via WiFi

## Arkitektur

```
┌─────────────────┐     ┌──────────────┐     ┌─────────────┐
│  SHL.se /       │ ──► │   Backend    │ ──► │   ESP32     │
│  HockeyAllsv.   │     │  (DevPi)     │     │   Display   │
└─────────────────┘     └──────────────┘     └─────────────┘
       Puppeteer           Port 3080            WiFi/JSON
```

## Flikar på displayen

| Flik | Beskrivning |
|------|-------------|
| **SHL** | Tabell med alla 14 lag, position, +/-, poäng |
| **Allsvenskan** | Samma format för HockeyAllsvenskan |
| **Matcher** | Kommande och spelade matcher |
| **⚙️ Inställningar** | Display, WiFi, backend-config |

## Inställningar (via touch)

- 🔆 Ljusstyrka (0-100%)
- 🎨 Kontrast (0-100%)
- 🌙 Färgtema (Mörkt / Ljust / Hockey Blå)
- 📶 WiFi SSID & lösenord
- 🌐 Backend URL
- 💾 Spara / Återställ till fabriksinställningar

## Backend

Backend körs som systemd-tjänst på DevPi.

```bash
# Status
sudo systemctl status hockey-panel

# Loggar
sudo journalctl -u hockey-panel -f

# Starta om
sudo systemctl restart hockey-panel
```

### API Endpoints

| Endpoint | Beskrivning |
|----------|-------------|
| `GET /api/status` | Status och poll-intervall |
| `GET /api/shl` | SHL tabell och matcher |
| `GET /api/allsvenskan` | Allsvenskan tabell och matcher |
| `GET /api/all` | Allt kombinerat |

### Manuell körning (dev)

```bash
cd backend
npm install
npm run dev      # Hot-reload
npm run build    # Bygg för produktion
npm start        # Kör produktion
```

## Firmware

### Första installation (USB)

```bash
cd firmware
source .venv/bin/activate

# Bygg
pio run

# Flasha via USB
pio run -t upload

# Serial monitor
pio device monitor
```

### OTA-uppdatering (WiFi)

Efter första flashen kan du uppdatera trådlöst:

```bash
# Via mDNS hostname
pio run -e ota -t upload

# Eller ändra IP i platformio.ini:
# upload_port = 192.168.1.xxx
```

**OTA-lösenord:** `hockey2026`

Under OTA-uppdatering visas en progress-bar på displayen.

## Konfiguration

### WiFi (första gången)

1. Flasha firmware via USB
2. Displayen startar utan WiFi
3. Gå till **Inställningar**-fliken
4. Skriv in WiFi SSID och lösenord
5. Tryck **Spara**
6. Enheten startar om och ansluter

### Backend URL

Standard: `http://192.168.1.223:3080` (DevPi)

Ändra i **Inställningar** om backend körs på annan maskin.

## Filstruktur

```
hockey-panel/
├── backend/
│   ├── src/
│   │   ├── server.ts           # Express API
│   │   └── scrapers/
│   │       ├── shl.ts          # SHL web scraper
│   │       └── allsvenskan.ts  # Allsvenskan scraper
│   ├── dist/                   # Kompilerad JS
│   ├── package.json
│   └── hockey-panel.service    # Systemd service
├── firmware/
│   ├── src/
│   │   ├── main.cpp            # Huvudprogram + LVGL UI
│   │   ├── display_config.h    # LovyanGFX config
│   │   └── settings.h          # Preferences manager
│   ├── platformio.ini
│   └── lv_conf.h               # LVGL config
└── README.md
```

## Felsökning

### Display svart
- Kolla USB-anslutning
- Tryck RESET på kortet
- Kolla serial monitor för fel

### Ingen WiFi
- Gå till Inställningar och kontrollera SSID/lösenord
- Kolla att routern är inom räckhåll

### Ingen data
- Verifiera backend körs: `curl http://devpi:3080/api/status`
- Kolla att Backend URL är korrekt i inställningar
- Kolla `journalctl -u hockey-panel` för backend-fel

### OTA misslyckas
- Verifiera att ESP32 och datorn är på samma nätverk
- Prova med IP istället för hostname
- Kolla att lösenordet är rätt (`hockey2026`)

## Licens

MIT
