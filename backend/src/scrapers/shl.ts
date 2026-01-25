import puppeteer from 'puppeteer-core';

export interface Team {
  position: number;
  name: string;
  played: number;
  wins: number;
  draws: number;  // OT/SO wins
  losses: number;
  otLosses: number;
  goalsFor: number;
  goalsAgainst: number;
  goalDiff: number;
  points: number;
}

export interface Match {
  homeTeam: string;
  awayTeam: string;
  homeScore: number | null;
  awayScore: number | null;
  date: string;
  time: string;
  status: 'upcoming' | 'live' | 'finished';
  venue?: string;
}

export interface NewsItem {
  title: string;
  summary: string;
  date: string;
  url: string;
}

export interface SHLData {
  standings: Team[];
  matches: Match[];
  news: NewsItem[];
  lastUpdated: string;
}

function findChromePath(): string {
  const paths = [
    '/usr/bin/chromium',
    '/usr/bin/chromium-browser',
    '/usr/bin/google-chrome',
    '/usr/bin/google-chrome-stable',
    '/snap/bin/chromium',
  ];
  
  for (const p of paths) {
    try {
      require('fs').accessSync(p);
      return p;
    } catch {}
  }
  throw new Error('Chrome/Chromium not found');
}

export async function scrapeSHL(): Promise<SHLData> {
  console.log('Scraping SHL data...');
  
  const browser = await puppeteer.launch({
    executablePath: findChromePath(),
    headless: true,
    args: ['--no-sandbox', '--disable-setuid-sandbox', '--disable-dev-shm-usage']
  });
  
  try {
    const page = await browser.newPage();
    await page.setUserAgent('Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36');
    
    // 1. Scrape standings from main page
    await page.goto('https://www.shl.se/', { 
      waitUntil: 'networkidle2',
      timeout: 30000 
    });
    
    await page.waitForSelector('table', { timeout: 10000 });
    
    // Get detailed standings
    const standings = await page.evaluate(() => {
      const teams: any[] = [];
      const rows = document.querySelectorAll('table tbody tr');
      
      rows.forEach((row, index) => {
        const cells = row.querySelectorAll('td');
        if (cells.length >= 4) {
          const nameCell = cells[0];
          const nameEl = nameCell.querySelector('p');
          const name = nameEl?.textContent?.trim() || '';
          
          // Try to get more columns if available
          const played = parseInt(cells[1]?.textContent?.trim() || '0');
          const goalDiff = parseInt(cells[2]?.textContent?.trim() || '0');
          const points = parseInt(cells[3]?.textContent?.trim() || '0');
          
          if (name && name.length > 1) {
            // Estimate W/L from points (rough)
            const winsApprox = Math.floor(points / 3);
            const otWins = points % 3;
            
            teams.push({ 
              position: index + 1,
              name, 
              played,
              wins: winsApprox,
              draws: otWins,
              losses: Math.max(0, played - winsApprox - otWins),
              otLosses: 0,
              goalsFor: 0,
              goalsAgainst: 0,
              goalDiff,
              points
            });
          }
        }
      });
      
      return teams;
    });
    
    // 2. Scrape matches
    const matches = await page.evaluate(() => {
      const matchList: any[] = [];
      
      // Try multiple selectors for matches
      const matchContainers = document.querySelectorAll('[class*="match"], [class*="game"], a[href*="game-center"]');
      
      matchContainers.forEach(el => {
        const text = el.textContent || '';
        // Try to parse match info
        const scoreMatch = text.match(/(\w[\wåäöÅÄÖ\s]+?)\s+(\d+)\s*[-–]\s*(\d+)\s+(\w[\wåäöÅÄÖ\s]+)/);
        const upcomingMatch = text.match(/(\w[\wåäöÅÄÖ\s]+?)\s+(\d{1,2}[:.]\d{2})\s+(\w[\wåäöÅÄÖ\s]+)/);
        
        if (scoreMatch) {
          matchList.push({
            homeTeam: scoreMatch[1].trim(),
            awayTeam: scoreMatch[4].trim(),
            homeScore: parseInt(scoreMatch[2]),
            awayScore: parseInt(scoreMatch[3]),
            date: '',
            time: '',
            status: 'finished'
          });
        } else if (upcomingMatch) {
          matchList.push({
            homeTeam: upcomingMatch[1].trim(),
            awayTeam: upcomingMatch[3].trim(),
            homeScore: null,
            awayScore: null,
            date: '',
            time: upcomingMatch[2],
            status: 'upcoming'
          });
        }
      });
      
      return matchList.slice(0, 20);
    });
    
    // 3. Scrape news from article archive
    let news: NewsItem[] = [];
    try {
      await page.goto('https://www.shl.se/article/archive', {
        waitUntil: 'networkidle2',
        timeout: 15000
      });
      
      await page.waitForSelector('article, [class*="article"], a[href*="/article/"]', { timeout: 5000 });
      
      news = await page.evaluate(() => {
        const newsItems: any[] = [];
        const articles = document.querySelectorAll('article, [class*="ArticleCard"], a[href*="/article/"]');
        
        articles.forEach(article => {
          const titleEl = article.querySelector('h2, h3, [class*="title"]');
          const summaryEl = article.querySelector('p, [class*="summary"], [class*="excerpt"]');
          const linkEl = article.querySelector('a[href*="/article/"]') || article;
          const dateEl = article.querySelector('time, [class*="date"]');
          
          const title = titleEl?.textContent?.trim() || '';
          const url = (linkEl as HTMLAnchorElement)?.href || '';
          
          if (title && title.length > 5) {
            newsItems.push({
              title,
              summary: summaryEl?.textContent?.trim()?.slice(0, 150) || '',
              date: dateEl?.textContent?.trim() || '',
              url
            });
          }
        });
        
        return newsItems.slice(0, 10);
      });
    } catch (e) {
      console.log('News scraping failed, continuing without news');
    }
    
    // Clean up standings
    const uniqueStandings = standings
      .filter((team, index, self) => 
        index === self.findIndex(t => t.name === team.name)
      )
      .slice(0, 14);
    
    console.log(`SHL: ${uniqueStandings.length} teams, ${matches.length} matches, ${news.length} news`);
    
    return {
      standings: uniqueStandings,
      matches,
      news,
      lastUpdated: new Date().toISOString()
    };
    
  } finally {
    await browser.close();
  }
}
