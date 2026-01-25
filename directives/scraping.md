# Directive: Web Scraping for Hockey Data

## Goal
Scrape current standings, matches, and news from SHL and HockeyAllsvenskan websites.

## Data Sources
- **SHL**: https://www.shl.se/ (standings table), https://www.shl.se/article/archive (news)
- **HockeyAllsvenskan**: https://www.hockeyallsvenskan.se/

## Tools
- `backend/src/scrapers/shl.ts` - SHL scraper
- `backend/src/scrapers/allsvenskan.ts` - Allsvenskan scraper

## Expected Output
Each scraper returns:
```typescript
{
  standings: Team[],      // Position, name, played, points, goalDiff, etc.
  matches: Match[],       // Home/away, score, status (upcoming/live/finished)
  news: NewsItem[],       // Title, summary, league
  lastUpdated: string     // ISO timestamp
}
```

## Execution

```bash
# Test SHL scraper
curl http://localhost:3080/api/shl | jq '.standings[:3]'

# Test Allsvenskan
curl http://localhost:3080/api/allsvenskan | jq '.standings[:3]'

# Full combined data
curl http://localhost:3080/api/all
```

## Common Issues

### Website structure changed
**Symptom**: Empty standings array or missing data
**Fix**: 
1. Open the source URL in browser
2. Inspect the table/element structure
3. Update selectors in the scraper
4. Test locally before deploying

### Rate limiting
**Symptom**: HTTP 429 or empty responses
**Fix**: Cache is already set to 5 minutes. If still hitting limits, increase `CACHE_TTL` in server.ts

### Puppeteer crashes
**Symptom**: "Chrome/Chromium not found" or timeout errors
**Fix**: Ensure chromium is installed: `sudo apt install chromium-browser`

## Data Quality Notes

- Goals for/against often unavailable from main page (set to 0)
- Win/loss is estimated from points when not directly available
- Match times may lack dates (shows time only)

## Learnings Log

*Add notes here when fixing issues:*

- 2026-01-25: Initial scrapers created. Tables work, matches parsing is hit-or-miss depending on page layout.
