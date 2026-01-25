"use strict";
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
Object.defineProperty(exports, "__esModule", { value: true });
const express_1 = __importDefault(require("express"));
const shl_1 = require("./scrapers/shl");
const allsvenskan_1 = require("./scrapers/allsvenskan");
const app = (0, express_1.default)();
const PORT = 3080;
const cache = {};
const CACHE_TTL = 5 * 60 * 1000; // 5 minuter
const LIVE_CACHE_TTL = 30 * 1000; // 30 sekunder under matcher
function isCacheValid(key, live = false) {
    const ttl = live ? LIVE_CACHE_TTL : CACHE_TTL;
    return cache[key] && (Date.now() - cache[key].timestamp) < ttl;
}
// Status endpoint - berättar för ESP32 om match pågår
app.get('/api/status', (req, res) => {
    res.json({
        ok: true,
        timestamp: Date.now(),
        liveMatch: false, // TODO: Detektera pågående matcher
        pollInterval: 300 // sekunder
    });
});
// SHL tabell och matcher
app.get('/api/shl', async (req, res) => {
    try {
        if (!isCacheValid('shl')) {
            cache['shl'] = {
                data: await (0, shl_1.scrapeSHL)(),
                timestamp: Date.now()
            };
        }
        res.json(cache['shl'].data);
    }
    catch (error) {
        console.error('SHL scrape error:', error);
        res.status(500).json({ error: 'Failed to fetch SHL data' });
    }
});
// Allsvenskan tabell och matcher
app.get('/api/allsvenskan', async (req, res) => {
    try {
        if (!isCacheValid('allsvenskan')) {
            cache['allsvenskan'] = {
                data: await (0, allsvenskan_1.scrapeAllsvenskan)(),
                timestamp: Date.now()
            };
        }
        res.json(cache['allsvenskan'].data);
    }
    catch (error) {
        console.error('Allsvenskan scrape error:', error);
        res.status(500).json({ error: 'Failed to fetch Allsvenskan data' });
    }
});
// Kombinerad endpoint för allt
app.get('/api/all', async (req, res) => {
    try {
        const [shl, allsvenskan] = await Promise.all([
            isCacheValid('shl') ? cache['shl'].data : (0, shl_1.scrapeSHL)(),
            isCacheValid('allsvenskan') ? cache['allsvenskan'].data : (0, allsvenskan_1.scrapeAllsvenskan)()
        ]);
        // Uppdatera cache
        if (!isCacheValid('shl')) {
            cache['shl'] = { data: shl, timestamp: Date.now() };
        }
        if (!isCacheValid('allsvenskan')) {
            cache['allsvenskan'] = { data: allsvenskan, timestamp: Date.now() };
        }
        res.json({ shl, allsvenskan, timestamp: Date.now() });
    }
    catch (error) {
        console.error('Scrape error:', error);
        res.status(500).json({ error: 'Failed to fetch data' });
    }
});
// News endpoint
app.get('/api/news', async (req, res) => {
    try {
        const [shl, allsvenskan] = await Promise.all([
            isCacheValid('shl') ? cache['shl'].data : (0, shl_1.scrapeSHL)(),
            isCacheValid('allsvenskan') ? cache['allsvenskan'].data : (0, allsvenskan_1.scrapeAllsvenskan)()
        ]);
        const allNews = [
            ...(shl.news || []).map((n) => ({ ...n, league: 'SHL' })),
            ...(allsvenskan.news || []).map((n) => ({ ...n, league: 'HA' }))
        ];
        res.json({ news: allNews, timestamp: Date.now() });
    }
    catch (error) {
        console.error('News error:', error);
        res.status(500).json({ error: 'Failed to fetch news' });
    }
});
app.listen(PORT, '0.0.0.0', () => {
    console.log(`🏒 Hockey Panel Backend running on http://0.0.0.0:${PORT}`);
    console.log('Endpoints:');
    console.log('  GET /api/status      - Status och poll-intervall');
    console.log('  GET /api/shl         - SHL tabell och matcher');
    console.log('  GET /api/allsvenskan - Allsvenskan tabell och matcher');
    console.log('  GET /api/all         - Allt kombinerat');
});
