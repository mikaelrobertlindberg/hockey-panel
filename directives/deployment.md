# Directive: Backend Deployment

## Goal
Run the hockey-panel backend as a persistent service on DevPi.

## Current Setup
- **Host**: DevPi (192.168.1.224)
- **Port**: 3080
- **Process**: Node.js with tsx

## Manual Start

```bash
cd /home/devpi/clawd/hockey-panel/backend
npm run dev
# or for production:
npx tsx src/server.ts
```

## Systemd Service (Recommended)

Create `/etc/systemd/system/hockey-panel.service`:

```ini
[Unit]
Description=Hockey Panel API
After=network.target

[Service]
Type=simple
User=devpi
WorkingDirectory=/home/devpi/clawd/hockey-panel/backend
ExecStart=/usr/bin/npx tsx src/server.ts
Restart=on-failure
RestartSec=10
Environment=NODE_ENV=production

[Install]
WantedBy=multi-user.target
```

Enable and start:
```bash
sudo systemctl daemon-reload
sudo systemctl enable hockey-panel
sudo systemctl start hockey-panel
```

## Check Status

```bash
# Service status
sudo systemctl status hockey-panel

# API health check
curl http://localhost:3080/api/status

# Logs
journalctl -u hockey-panel -f
```

## Dependencies

Backend requires:
- Node.js 18+
- Chromium (for Puppeteer)

```bash
sudo apt install chromium-browser
cd backend && npm install
```

## Troubleshooting

### Port already in use
```bash
lsof -i :3080
kill <PID>
```

### Puppeteer can't find Chrome
```bash
which chromium || which chromium-browser
# Update findChromePath() in scrapers if needed
```

## Learnings Log

- 2026-01-25: Backend runs on port 3080, requires chromium for scraping
