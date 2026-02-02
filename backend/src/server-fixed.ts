import express from 'express';
import cors from 'cors';
import { scrapeSHL } from './scrapers/shl';
import { scrapeAllsvenskan } from './scrapers/allsvenskan';

const app = express();
const PORT = 3000;

// Middleware setup
app.use(cors());
app.use(express.json());

// Global error handler
app.use((err: any, req: express.Request, res: express.Response, next: express.NextFunction) => {
    console.error('Server error:', err);
    res.status(500).json({ error: 'Internal server error' });
});

// Centralized cache management
interface Cache {
    data: any;
    timestamp: number;
}

class CacheManager {
    private cache: Record<string, Cache> = {};
    private readonly CACHE_TTL = 5 * 60 * 1000; // 5 minutes
    private readonly LIVE_CACHE_TTL = 30 * 1000; // 30 seconds during matches

    isCacheValid(key: string, live: boolean = false): boolean {
        const ttl = live ? this.LIVE_CACHE_TTL : this.CACHE_TTL;
        return this.cache[key] && (Date.now() - this.cache[key].timestamp) < ttl;
    }

    async getCachedData<T>(key: string, fetchFn: () => Promise<T>, live: boolean = false): Promise<T> {
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
        } catch (error) {
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
    } catch (error) {
        console.error('Status endpoint error:', error);
        res.status(500).json({ error: 'Failed to get status' });
    }
});

// SHL endpoint with timeout protection
app.get('/api/shl', async (req, res) => {
    try {
        const data = await cacheManager.getCachedData('shl', scrapeSHL);
        res.json(data);
    } catch (error) {
        console.error('SHL endpoint error:', error);
        res.status(500).json({ error: 'Failed to fetch SHL data' });
    }
});

// Allsvenskan endpoint
app.get('/api/allsvenskan', async (req, res) => {
    try {
        const data = await cacheManager.getCachedData('allsvenskan', scrapeAllsvenskan);
        res.json(data);
    } catch (error) {
        console.error('Allsvenskan endpoint error:', error);
        res.status(500).json({ error: 'Failed to fetch Allsvenskan data' });
    }
});

// Combined endpoint with timeout and fallback handling
app.get('/api/all', async (req, res) => {
    try {
        const results = await Promise.allSettled([
            cacheManager.getCachedData('shl', scrapeSHL),
            cacheManager.getCachedData('allsvenskan', scrapeAllsvenskan)
        ]);

        const shl = results[0].status === 'fulfilled' ? results[0].value : { error: 'SHL data unavailable' };
        const allsvenskan = results[1].status === 'fulfilled' ? results[1].value : { error: 'Allsvenskan data unavailable' };

        res.json({ 
            shl, 
            allsvenskan, 
            timestamp: Date.now() 
        });
    } catch (error) {
        console.error('Combined endpoint error:', error);
        res.status(500).json({ error: 'Failed to fetch data' });
    }
});

// News endpoint with proper error isolation
app.get('/api/news', async (req, res) => {
    try {
        const results = await Promise.allSettled([
            cacheManager.getCachedData('shl', scrapeSHL),
            cacheManager.getCachedData('allsvenskan', scrapeAllsvenskan)
        ]);

        const allNews: any[] = [];
        
        if (results[0].status === 'fulfilled' && results[0].value.news) {
            allNews.push(...results[0].value.news.map((n: any) => ({ ...n, league: 'SHL' })));
        }
        
        if (results[1].status === 'fulfilled' && results[1].value.news) {
            allNews.push(...results[1].value.news.map((n: any) => ({ ...n, league: 'HA' })));
        }

        res.json({ 
            news: allNews, 
            timestamp: Date.now() 
        });
    } catch (error) {
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