// Emergency mock hockey API server
const express = require('express');
const app = express();

// Simple CORS headers
app.use((req, res, next) => {
    res.header('Access-Control-Allow-Origin', '*');
    res.header('Access-Control-Allow-Headers', 'Accept-Charset, Content-Type');
    next();
});
app.use(express.json());

// Mock SHL teams data
const mockSHLTeams = [
    { name: "Frölunda HC", position: 1, points: 85, played: 35, goalDiff: 25, wins: 25, draws: 8, losses: 2, goalsFor: 125, goalsAgainst: 100 },
    { name: "Skellefteå AIK", position: 2, points: 82, played: 35, goalDiff: 22, wins: 24, draws: 6, losses: 5, goalsFor: 122, goalsAgainst: 100 },
    { name: "Luleå Hockey", position: 3, points: 78, played: 35, goalDiff: 18, wins: 22, draws: 10, losses: 3, goalsFor: 118, goalsAgainst: 100 }
];

// Mock HA teams data  
const mockHATeams = [
    { name: "BIK Karlskoga", position: 1, points: 75, played: 35, goalDiff: 20, wins: 20, draws: 15, losses: 0, goalsFor: 115, goalsAgainst: 95 },
    { name: "Västerås IK", position: 2, points: 72, played: 35, goalDiff: 17, wins: 19, draws: 12, losses: 4, goalsFor: 112, goalsAgainst: 95 }
];

// Mock matches
const mockMatches = [
    { homeTeam: "Frölunda HC", awayTeam: "Skellefteå AIK", homeScore: 3, awayScore: 2, time: "19:00", status: "Slutspelat", isSHL: true }
];

// Mock news
const mockNews = [
    { title: "Frölunda vann mot Skellefteå", url: "https://www.shl.se", time: "18:30", category: "SHL" }
];

// API Endpoints
app.get('/api/status', (req, res) => {
    res.json({
        ok: true,
        timestamp: Date.now(),
        liveMatch: false,
        pollInterval: 300
    });
});

app.get('/api/shl', (req, res) => {
    res.json({
        teams: mockSHLTeams,
        matches: mockMatches.filter(m => m.isSHL),
        news: mockNews
    });
});

app.get('/api/allsvenskan', (req, res) => {
    res.json({
        teams: mockHATeams,
        matches: mockMatches.filter(m => !m.isSHL),
        news: []
    });
});

app.get('/api/all', (req, res) => {
    res.json({
        shl: {
            teams: mockSHLTeams,
            matches: mockMatches.filter(m => m.isSHL),
            news: mockNews
        },
        allsvenskan: {
            teams: mockHATeams,
            matches: mockMatches.filter(m => !m.isSHL), 
            news: []
        }
    });
});

const PORT = 3080;
app.listen(PORT, '0.0.0.0', () => {
    console.log(`🏒 Emergency Mock Hockey API running on http://0.0.0.0:${PORT}`);
    console.log('Serving mock data for ESP32 hockey panel');
});