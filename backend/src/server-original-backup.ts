import express from 'express';
import { scrapeSHL } from './scrapers/shl';
import { scrapeAllsvenskan } from './scrapers/allsvenskan';

const app = express();
const PORT = 3000;

// Cache för att inte hamra källsidorna
interface Cache {
  data: any;
  timestamp: number;
}

const cache: Record<string, Cache> = {};
const CACHE_TTL = 5 * 60 * 1000; // 5 minuter
const LIVE_CACHE_TTL = 30 * 1000; // 30 sekunder under matcher

function isCacheValid(key: string, live: boolean = false): boolean {
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
        data: await scrapeSHL(),
        timestamp: Date.now()
      };
    }
    res.json(cache['shl'].data);
  } catch (error) {
    console.error('SHL scrape error:', error);
    res.status(500).json({ error: 'Failed to fetch SHL data' });
  }
});

// Allsvenskan tabell och matcher
app.get('/api/allsvenskan', async (req, res) => {
  try {
    if (!isCacheValid('allsvenskan')) {
      cache['allsvenskan'] = {
        data: await scrapeAllsvenskan(),
        timestamp: Date.now()
      };
    }
    res.json(cache['allsvenskan'].data);
  } catch (error) {
    console.error('Allsvenskan scrape error:', error);
    res.status(500).json({ error: 'Failed to fetch Allsvenskan data' });
  }
});

// Kombinerad endpoint för allt - SHL ÅR NU FIXAT!
app.get('/api/all', async (req, res) => {
  try {
    const [shl, allsvenskan] = await Promise.all([
      isCacheValid('shl') ? cache['shl'].data : scrapeSHL(),
      isCacheValid('allsvenskan') ? cache['allsvenskan'].data : scrapeAllsvenskan()
    ]);
    
    // Uppdatera cache
    if (!isCacheValid('shl')) {
      cache['shl'] = { data: shl, timestamp: Date.now() };
    }
    if (!isCacheValid('allsvenskan')) {
      cache['allsvenskan'] = { data: allsvenskan, timestamp: Date.now() };
    }
    
    res.json({ shl, allsvenskan, timestamp: Date.now() });
  } catch (error) {
    console.error('Scrape error:', error);
    res.status(500).json({ error: 'Failed to fetch data' });
  }
});

// News endpoint
app.get('/api/news', async (req, res) => {
  try {
    const [shl, allsvenskan] = await Promise.all([
      isCacheValid('shl') ? cache['shl'].data : scrapeSHL(),
      isCacheValid('allsvenskan') ? cache['allsvenskan'].data : scrapeAllsvenskan()
    ]);
    
    const allNews = [
      ...(shl.news || []).map((n: any) => ({ ...n, league: 'SHL' })),
      ...(allsvenskan.news || []).map((n: any) => ({ ...n, league: 'HA' }))
    ];
    
    res.json({ news: allNews, timestamp: Date.now() });
  } catch (error) {
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
