"use strict";
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
Object.defineProperty(exports, "__esModule", { value: true });
const express_1 = __importDefault(require("express"));
const cors_1 = __importDefault(require("cors"));
const shl_1 = require("./scrapers/shl");
const allsvenskan_1 = require("./scrapers/allsvenskan");
const app = (0, express_1.default)();
const PORT = 3000;
// Middleware setup
app.use((0, cors_1.default)());
app.use(express_1.default.json());
// Global error handler
app.use((err, req, res, next) => {
    console.error('Server error:', err);
    res.status(500).json({ error: 'Internal server error' });
});
class CacheManager {
    cache = {};
    CACHE_TTL = 5 * 60 * 1000; // 5 minutes
    LIVE_CACHE_TTL = 30 * 1000; // 30 seconds during matches
    isCacheValid(key, live = false) {
        const ttl = live ? this.LIVE_CACHE_TTL : this.CACHE_TTL;
        return this.cache[key] && (Date.now() - this.cache[key].timestamp) < ttl;
    }
    async getCachedData(key, fetchFn, live = false) {
        if (this.isCacheValid(key, live)) {
            console.log(`Cache hit for ${key}`);
            return this.cache[key].data;
        }
        console.log(`Cache miss for ${key}, fetching...`);
        try {
            const data = await fetchFn();
            this.cache[key] = {
                data,
                timestamp: Date.now()
            };
            return data;
        }
        catch (error) {
            console.error(`Error fetching ${key}:`, error);
            // Return stale cache if available
            if (this.cache[key]) {
                console.log(`Returning stale cache for ${key}`);
                return this.cache[key].data;
            }
            throw error;
        }
    }
}
const cacheManager = new CacheManager();
// Status endpoint with proper error handling
app.get('/api/status', (req, res) => {
    try {
        res.json({
            ok: true,
            timestamp: Date.now(),
            liveMatch: false, // TODO: Implement live match detection
            pollInterval: 300 // seconds
        });
    }
    catch (error) {
        console.error('Status endpoint error:', error);
        res.status(500).json({ error: 'Failed to get status' });
    }
});
// SHL endpoint with timeout protection
app.get('/api/shl', async (req, res) => {
    try {
        const data = await cacheManager.getCachedData('shl', shl_1.scrapeSHL);
        res.json(data);
    }
    catch (error) {
        console.error('SHL endpoint error:', error);
        res.status(500).json({ error: 'Failed to fetch SHL data' });
    }
});
// Allsvenskan endpoint
app.get('/api/allsvenskan', async (req, res) => {
    try {
        const data = await cacheManager.getCachedData('allsvenskan', allsvenskan_1.scrapeAllsvenskan);
        res.json(data);
    }
    catch (error) {
        console.error('Allsvenskan endpoint error:', error);
        res.status(500).json({ error: 'Failed to fetch Allsvenskan data' });
    }
});
// Combined endpoint with timeout and fallback handling
app.get('/api/all', async (req, res) => {
    try {
        const results = await Promise.allSettled([
            cacheManager.getCachedData('shl', shl_1.scrapeSHL),
            cacheManager.getCachedData('allsvenskan', allsvenskan_1.scrapeAllsvenskan)
        ]);
        const shl = results[0].status === 'fulfilled' ? results[0].value : { error: 'SHL data unavailable' };
        const allsvenskan = results[1].status === 'fulfilled' ? results[1].value : { error: 'Allsvenskan data unavailable' };
        res.json({
            shl,
            allsvenskan,
            timestamp: Date.now()
        });
    }
    catch (error) {
        console.error('Combined endpoint error:', error);
        res.status(500).json({ error: 'Failed to fetch data' });
    }
});
// News endpoint with proper error isolation
app.get('/api/news', async (req, res) => {
    try {
        const results = await Promise.allSettled([
            cacheManager.getCachedData('shl', shl_1.scrapeSHL),
            cacheManager.getCachedData('allsvenskan', allsvenskan_1.scrapeAllsvenskan)
        ]);
        const allNews = [];
        if (results[0].status === 'fulfilled' && results[0].value.news) {
            allNews.push(...results[0].value.news.map((n) => ({ ...n, league: 'SHL' })));
        }
        if (results[1].status === 'fulfilled' && results[1].value.news) {
            allNews.push(...results[1].value.news.map((n) => ({ ...n, league: 'HA' })));
        }
        res.json({
            news: allNews,
            timestamp: Date.now()
        });
    }
    catch (error) {
        console.error('News endpoint error:', error);
        res.status(500).json({ error: 'Failed to fetch news' });
    }
});
// Graceful shutdown
process.on('SIGINT', () => {
    console.log('Shutting down gracefully...');
    process.exit(0);
});
process.on('SIGTERM', () => {
    console.log('Shutting down gracefully...');
    process.exit(0);
});
app.listen(PORT, '0.0.0.0', () => {
    console.log(`🏒 Hockey Panel Backend running on http://0.0.0.0:${PORT}`);
    console.log('Endpoints:');
    console.log('  GET /api/status      - Status och poll-intervall');
    console.log('  GET /api/shl         - SHL tabell och matcher');
    console.log('  GET /api/allsvenskan - Allsvenskan tabell och matcher');
    console.log('  GET /api/all         - Allt kombinerat');
    console.log('  GET /api/news        - Kombinerade nyheter');
});
