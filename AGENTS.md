# Hockey Panel - Agent Instructions

> Project-specific AGENTS.md for the Hockey Panel ESP32 display.

## Project Overview

A small ESP32-based display (CYD - Cheap Yellow Display) that shows Swedish hockey standings, upcoming matches, and news from SHL and HockeyAllsvenskan.

## Architecture

```
hockey-panel/
├── AGENTS.md          # This file
├── README.md          # Project documentation
├── directives/        # SOPs for scraping, firmware updates, etc.
├── execution/         # Utility scripts (deployment, testing)
├── .tmp/              # Intermediate files (scraped data, build artifacts)
├── backend/           # Node.js API server (runs on DevPi)
│   └── src/
│       ├── server.ts
│       └── scrapers/  # SHL + Allsvenskan scrapers
└── firmware/          # ESP32 PlatformIO project
    └── src/
        └── main.cpp
```

## Key Components

### Backend (Port 3080)
- Express server with caching
- Puppeteer-based scrapers for shl.se and hockeyallsvenskan.se
- Endpoints: `/api/status`, `/api/shl`, `/api/allsvenskan`, `/api/all`, `/api/news`
- Cache TTL: 5 min normal, 30s during live matches

### Firmware (ESP32-2432S028)
- LovyanGFX display driver
- 4 screens: SHL table, Allsvenskan table, Upcoming matches, News
- Touch calibration with NVS persistence
- OTA updates (hostname: HockeyPanel)
- WiFi: IoT network

## Directives

See `directives/` for:
- `scraping.md` - How to maintain/fix scrapers
- `firmware.md` - Building and flashing firmware
- `deployment.md` - Running the backend service

## Development

```bash
# Backend
cd backend && npm run dev

# Firmware
cd firmware && pio run -t upload

# OTA flash
pio run -t upload --upload-port HockeyPanel.local
```

## Self-Annealing Notes

When scrapers break (website structure changes):
1. Check error logs
2. Update selectors in `backend/src/scrapers/*.ts`
3. Test with `curl http://localhost:3080/api/shl`
4. Update `directives/scraping.md` with learnings
