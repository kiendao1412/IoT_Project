// ==============================
// GPS Tracker - Leaflet + OSM
// ==============================

// ========= MOCK CONFIG =========
const MOCK_CENTER = { lat: 10.7769, lng: 106.7009 };
let mockTick = 0;

function getMockPoint() {
  mockTick++;
  const radius = 0.0025;
  const angle = mockTick * 0.35;
  const lat = MOCK_CENTER.lat + Math.sin(angle) * radius;
  const lng = MOCK_CENTER.lng + Math.cos(angle) * radius;
  return {
    source: "mock",
    created_at: new Date().toISOString(),
    lat: +lat.toFixed(6),
    lng: +lng.toFixed(6)
  };
}

// ========= MAP (Leaflet) =========
let map = null;
let marker = null;
let polyline = null;
let pathCoords = []; // [[lat,lng], ...]
let lastPos = null;

function initMap() {
  map = L.map("map", { zoomControl: true }).setView(
    [MOCK_CENTER.lat, MOCK_CENTER.lng],
    15
  );

  L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png", {
    maxZoom: 19,
    attribution: "&copy; OpenStreetMap contributors"
  }).addTo(map);

  marker = L.marker([MOCK_CENTER.lat, MOCK_CENTER.lng]).addTo(map);
  polyline = L.polyline(pathCoords).addTo(map);

  setRunState(false, "Chưa chạy");
}

function setRunState(isRunning, tooltipText) {
  const dot = document.getElementById("statusDot");
  if (!dot) return;

  dot.classList.remove("statusOn", "statusOff");
  dot.classList.add(isRunning ? "statusOn" : "statusOff");
  dot.title = tooltipText || (isRunning ? "Đang chạy" : "Chưa chạy");
}

function updateMap(lat, lng) {
  if (!map || !marker || !polyline) return;

  lastPos = { lat, lng };

  marker.setLatLng([lat, lng]);
  map.panTo([lat, lng]);

  pathCoords.push([lat, lng]);
  if (pathCoords.length > 200) pathCoords.shift();
  polyline.setLatLngs(pathCoords);
}

function resetMapView() {
  pathCoords = [];
  if (polyline) polyline.setLatLngs(pathCoords);

  const center = lastPos || MOCK_CENTER;
  if (marker) marker.setLatLng([center.lat, center.lng]);

  if (map) {
    map.setView([center.lat, center.lng], 15);
  }
}

// ========= UI =========
function setCurrentInfo(lat, lng, createdAt) {
  const latEl = document.getElementById("curLat");
  const lngEl = document.getElementById("curLng");
  const timeEl = document.getElementById("curTime");

  if (latEl) latEl.textContent = Number(lat).toFixed(6);
  if (lngEl) lngEl.textContent = Number(lng).toFixed(6);
  if (timeEl) timeEl.textContent = createdAt || "-";
}

function clearCurrentInfo() {
  setCurrentInfo("-", "-", "-");
}

function clearHistory() {
  const body = document.getElementById("historyBody");
  if (body) body.innerHTML = "";
}

function renderHistory(points) {
  const body = document.getElementById("historyBody");
  if (!body) return;

  body.innerHTML = "";
  for (const p of points) {
    const tr = document.createElement("tr");
    tr.innerHTML = `
      <td>${p.created_at ?? "-"}</td>
      <td>${Number(p.lat).toFixed(6)}</td>
      <td>${Number(p.lng).toFixed(6)}</td>
    `;
    body.appendChild(tr);
  }
}

// ========= ThingSpeak proxy fetch =========
async function fetchProxyLast() {
  const res = await fetch("/api/last", { cache: "no-store" });
  const data = await res.json();
  if (!res.ok) throw new Error(data?.error || `HTTP ${res.status}`);
  return data; // { created_at, lat, lng }
}

async function fetchProxyHistory(results = 10) {
  const res = await fetch(`/api/history?results=${encodeURIComponent(results)}`, { cache: "no-store" });
  const data = await res.json();
  if (!res.ok) throw new Error(data?.error || `HTTP ${res.status}`);
  return Array.isArray(data?.points) ? data.points : [];
}

// ========= APP LOOP =========
let timer = null;

function getMode() {
  const el = document.getElementById("dataMode");
  return el ? el.value : "mock";
}

function getRefreshSec() {
  const el = document.getElementById("refreshSec");
  const n = Number(el?.value || 20);
  return Math.max(2, n);
}

async function fetchDataOnce() {
  const mode = getMode();

  if (mode === "mock") {
    const last = getMockPoint();
    const history = Array.from({ length: 20 }, () => getMockPoint()).reverse();
    return { last, history };
  }

  const last = await fetchProxyLast();
  const history = await fetchProxyHistory(20);
  return { last, history };
}

async function fetchAndRender() {
  try {
    const { last, history } = await fetchDataOnce();
    setCurrentInfo(last.lat, last.lng, last.created_at);
    renderHistory(history);
    updateMap(Number(last.lat), Number(last.lng));
  } catch (err) {
    console.error(err);
  }
}

function startLoop() {
  stopLoop();
  const sec = getRefreshSec();
  setRunState(true, "Đang chạy");
  fetchAndRender();
  timer = setInterval(fetchAndRender, sec * 1000);
}

function stopLoop() {
  if (timer) clearInterval(timer);
  timer = null;
  setRunState(false, "Chưa chạy");
}

// ========= BOOTSTRAP =========
document.addEventListener("DOMContentLoaded", () => {
  initMap();

  const btnStart = document.getElementById("btnStart");
  const btnStop = document.getElementById("btnStop");
  const btnRefresh = document.getElementById("btnRefresh");

  if (btnStart) btnStart.addEventListener("click", startLoop);
  if (btnStop) btnStop.addEventListener("click", stopLoop);

  if (btnRefresh) {
    btnRefresh.addEventListener("click", async () => {
      // Refresh = reset map + clear UI + fetch 1 lần
      resetMapView();
      clearHistory();
      clearCurrentInfo();
      await fetchAndRender();
    });
  }
});
