import express from "express";
import dotenv from "dotenv";
dotenv.config();

const app = express();
const PORT = Number(process.env.PORT || 3000);

const CHANNEL_ID = process.env.TS_CHANNEL_ID || "";
const READ_KEY = process.env.TS_READ_KEY || "";
const LAT_FIELD = process.env.TS_LAT_FIELD || "field1";
const LNG_FIELD = process.env.TS_LNG_FIELD || "field2";

app.use(express.static("public", { extensions: ["html"] }));

function buildThingSpeakUrl(pathAndQuery) {
  if (!CHANNEL_ID) throw new Error("Missing TS_CHANNEL_ID in .env");

  const base = `https://api.thingspeak.com/channels/${encodeURIComponent(CHANNEL_ID)}/${pathAndQuery}`;
  if (!READ_KEY) return base;

  return base.includes("?")
    ? `${base}&api_key=${encodeURIComponent(READ_KEY)}`
    : `${base}?api_key=${encodeURIComponent(READ_KEY)}`;
}

function parseLatLng(payload) {
  const latRaw = payload?.[LAT_FIELD];
  const lngRaw = payload?.[LNG_FIELD];
  const lat = Number(latRaw);
  const lng = Number(lngRaw);

  if (!Number.isFinite(lat) || !Number.isFinite(lng)) {
    const e = new Error(`Invalid lat/lng from ThingSpeak. lat=${latRaw} lng=${lngRaw}`);
    e.statusCode = 422;
    throw e;
  }
  if (lat < -90 || lat > 90 || lng < -180 || lng > 180) {
    const e = new Error(`Out-of-range lat/lng. lat=${lat} lng=${lng}`);
    e.statusCode = 422;
    throw e;
  }
  return { lat, lng };
}

// last point
app.get("/api/last", async (req, res) => {
  try {
    const url = buildThingSpeakUrl("feeds/last.json");
    const r = await fetch(url, { cache: "no-store" });
    const data = await r.json();

    if (!r.ok) return res.status(r.status).json({ error: "ThingSpeak error", details: data });

    const { lat, lng } = parseLatLng(data);
    return res.json({
      source: "thingspeak",
      created_at: data.created_at ?? null,
      lat,
      lng
    });
  } catch (e) {
    return res.status(e.statusCode || 500).json({ error: e.message });
  }
});

// history
app.get("/api/history", async (req, res) => {
  try {
    const results = Math.min(100, Math.max(1, Number(req.query.results || 10)));
    const url = buildThingSpeakUrl(`feeds.json?results=${encodeURIComponent(results)}`);
    const r = await fetch(url, { cache: "no-store" });
    const data = await r.json();

    if (!r.ok) return res.status(r.status).json({ error: "ThingSpeak error", details: data });

    const feeds = Array.isArray(data?.feeds) ? data.feeds : [];
    const points = feeds
      .map(f => {
        const lat = Number(f?.[LAT_FIELD]);
        const lng = Number(f?.[LNG_FIELD]);
        return {
          created_at: f?.created_at ?? null,
          lat: Number.isFinite(lat) ? lat : null,
          lng: Number.isFinite(lng) ? lng : null
        };
      })
      .filter(x => x.lat !== null && x.lng !== null)
      .reverse(); // newest first

    return res.json({ source: "thingspeak", count: points.length, points });
  } catch (e) {
    return res.status(e.statusCode || 500).json({ error: e.message });
  }
});

app.listen(PORT, () => {
  console.log(`✅ Server running: http://localhost:${PORT}`);
});
