function apiCall(url, body) {
    fetch(url, {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify(body)
    })
        .then(response => response.json())
        .then(data => {
            console.log('Success:', data);
            if (data.error) alert(data.error);
        })
        .catch((error) => {
            console.error('Error:', error);
        });
}

function sendIR(code) {
    apiCall('/api/ir/send', { code: code });
}

function sendLED(cmd) {
    apiCall('/api/led/cmd', { cmd: cmd });
}

function sendColor(val) {
    apiCall('/api/led/cmd', { cmd: 'rgb', val: val });
}

/** LED effect/mode (Rainbow, Red Pulse, etc.) – sends mode + speed, not rgb. */
function sendLEDMode(mode, speed) {
    speed = speed ?? getLEDEffectSpeed();
    apiCall('/api/led/cmd', { cmd: 'mode', mode: mode, speed: speed });
}

function getLEDEffectSpeed() {
    const el = document.getElementById('led-effect-speed');
    return el ? parseInt(el.value, 10) || 50 : 50;
}

// --- ONO Display ---
function sendOnoText() {
    const text = document.getElementById('ono-text-input')?.value?.trim() || '';
    const duration = parseInt(document.getElementById('ono-text-duration')?.value, 10) || 5;
    if (!text) { alert('Enter text'); return; }
    apiCall('/api/ono/text', { text, duration });
}

function sendOnoRainbow() {
    const duration = parseInt(document.getElementById('ono-rainbow-duration')?.value, 10) || 10;
    apiCall('/api/ono/rainbow', { duration });
}

function sendOnoColor() {
    const hex = document.getElementById('ono-color-picker')?.value || '#ff0000';
    const r = parseInt(hex.slice(1, 3), 16);
    const g = parseInt(hex.slice(3, 5), 16);
    const b = parseInt(hex.slice(5, 7), 16);
    const duration = parseInt(document.getElementById('ono-color-duration')?.value, 10) || 10;
    apiCall('/api/ono/color', { r, g, b, duration });
}

function sendLEDRaw(hex) {
    hex = (hex || '').trim().replace(/^0x/i, '');
    if (!hex) { alert('Enter hex payload'); return; }
    apiCall('/api/led/raw', { hex: hex });
}

function sendHydration(cmd) {
    fetch('/api/hydration/cmd', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ cmd: cmd })
    })
        .then(response => response.json())
        .then(data => {
            if (data.error) {
                alert(data.error);
                return;
            }
            const statusEl = document.getElementById('hydration-cmd-status');
            if (statusEl && data.status) {
                const label = (data.status === 'led_on') ? 'LED ON' : (data.status === 'led_off') ? 'LED OFF' : (data.status === 'buzzer_on') ? 'Buzzer ON' : (data.status === 'buzzer_off') ? 'Buzzer OFF' : data.status;
                statusEl.textContent = label + ' sent';
                statusEl.style.visibility = 'visible';
                setTimeout(() => { statusEl.style.visibility = 'hidden'; }, 2000);
            }
        })
        .catch(err => {
            console.error('Hydration cmd failed:', err);
            alert('Request failed');
        });
}

function sendHydrationCmd(cmd, val = 0) {
    apiCall('/api/hydration/cmd', { cmd: cmd, val: val });
}

function toggleHydrationAdvanced() {
    const p = document.getElementById('hyd-advanced');
    p.style.display = p.style.display === 'none' ? 'block' : 'none';
}

function sendAIO(device, action) {
    apiCall('/api/aio/cmd', { device: device, action: action });
}

function sendServoSpray() {
    const btn = document.getElementById('spray-btn');
    if (btn) {
        btn.disabled = true;
        btn.innerHTML = '<i class="fa-solid fa-spinner fa-spin"></i> Spraying...';
    }
    fetch('/api/servo-spray', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ async: true })
    })
        .then(r => r.json())
        .then(data => {
            if (data.error) alert(data.error);
            if (btn) {
                btn.disabled = false;
                btn.innerHTML = '<i class="fa-solid fa-spray-can-sparkles"></i> Trigger Spray';
            }
        })
        .catch(err => {
            console.error('Spray failed:', err);
            alert('Spray request failed');
            if (btn) {
                btn.disabled = false;
                btn.innerHTML = '<i class="fa-solid fa-spray-can-sparkles"></i> Trigger Spray';
            }
        });
}

function masterOn() {
    apiCall('/api/master/cmd', { action: 'on' });
}

function masterOff() {
    apiCall('/api/master/cmd', { action: 'off' });
}

const HYDRATION_GOAL_STORAGE_KEY = 'hydration_daily_goal_ml';
const HYDRATION_GOAL_MIN = 500;
const HYDRATION_GOAL_MAX = 6000;
const WEIGHT_HISTORY_WINDOW_MS = 2 * 60 * 1000;

let hydrationGoalMl = loadHydrationGoal();
let latestHydrationData = null;
const weightHistory = [];

function loadHydrationGoal() {
    try {
        const raw = localStorage.getItem(HYDRATION_GOAL_STORAGE_KEY);
        const parsed = parseInt(raw, 10);
        if (!Number.isFinite(parsed)) return 2000;
        return Math.max(HYDRATION_GOAL_MIN, Math.min(HYDRATION_GOAL_MAX, parsed));
    } catch (e) {
        return 2000;
    }
}

function saveHydrationGoal() {
    try {
        localStorage.setItem(HYDRATION_GOAL_STORAGE_KEY, String(hydrationGoalMl));
    } catch (e) {}
}

function adjustHydrationGoal(delta) {
    hydrationGoalMl = Math.max(HYDRATION_GOAL_MIN, Math.min(HYDRATION_GOAL_MAX, hydrationGoalMl + delta));
    saveHydrationGoal();
    renderGoal(latestHydrationData ? latestHydrationData.daily_total_ml : 0);
}

function formatTimestamp(epochSec) {
    if (!epochSec || epochSec <= 0) return '--';
    const d = new Date(epochSec * 1000);
    return d.toLocaleString([], {
        month: 'short',
        day: '2-digit',
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit'
    });
}

function formatAgo(epochSec) {
    if (!epochSec || epochSec <= 0) return '--';
    const diff = Math.max(0, Math.floor(Date.now() / 1000 - epochSec));
    if (diff < 5) return 'just now';
    if (diff < 60) return diff + 's ago';
    if (diff < 3600) return Math.floor(diff / 60) + 'm ago';
    if (diff < 86400) {
        const h = Math.floor(diff / 3600);
        const m = Math.floor((diff % 3600) / 60);
        return h + 'h ' + m + 'm ago';
    }
    return Math.floor(diff / 86400) + 'd ago';
}

function appendWeightHistory(weight) {
    if (!Number.isFinite(weight)) return;
    const now = Date.now();
    weightHistory.push({ t: now, w: weight });
    while (weightHistory.length > 0 && (now - weightHistory[0].t) > WEIGHT_HISTORY_WINDOW_MS) {
        weightHistory.shift();
    }
    if (weightHistory.length > 120) {
        weightHistory.splice(0, weightHistory.length - 120);
    }
}

function renderWeightTrend() {
    const line = document.getElementById('hyd-sparkline-line');
    const deltaEl = document.getElementById('hyd-weight-delta');
    if (!line || !deltaEl) return;

    if (weightHistory.length < 2) {
        line.setAttribute('points', '');
        deltaEl.textContent = '--';
        deltaEl.className = 'trend-delta neutral';
        return;
    }

    const values = weightHistory.map(p => p.w);
    const min = Math.min(...values);
    const max = Math.max(...values);
    const range = Math.max(1, max - min);
    const n = values.length;
    const points = values.map((v, i) => {
        const x = (i / (n - 1)) * 100;
        const y = 28 - (((v - min) / range) * 24);
        return x.toFixed(2) + ',' + y.toFixed(2);
    }).join(' ');
    line.setAttribute('points', points);

    const delta = values[n - 1] - values[0];
    const sign = delta > 0 ? '+' : '';
    deltaEl.textContent = sign + delta.toFixed(1) + ' g / 2m';
    deltaEl.className = 'trend-delta ' + (delta > 0.2 ? 'up' : (delta < -0.2 ? 'down' : 'neutral'));
}

function renderGoal(totalMl) {
    const goalValueEl = document.getElementById('hyd-goal-value');
    const fillEl = document.getElementById('hyd-goal-fill');
    const captionEl = document.getElementById('hyd-goal-caption');
    if (!goalValueEl || !fillEl || !captionEl) return;

    const total = Number.isFinite(totalMl) ? totalMl : 0;
    const percent = Math.max(0, Math.min(100, (total / hydrationGoalMl) * 100));
    goalValueEl.textContent = hydrationGoalMl + ' ml';
    fillEl.style.width = percent.toFixed(1) + '%';
    captionEl.textContent = total.toFixed(1) + ' / ' + hydrationGoalMl + ' ml (' + percent.toFixed(0) + '%)';
}

function renderHydrationMeta(h) {
    const lastDrinkTimeEl = document.getElementById('hyd-last-drink-time');
    const lastDrinkAgoEl = document.getElementById('hyd-last-drink-ago');
    const freshnessEl = document.getElementById('hyd-freshness');
    const presenceStateEl = document.getElementById('hyd-presence-state');
    const presenceTimeEl = document.getElementById('hyd-presence-time');
    const presenceMethodEl = document.getElementById('hyd-presence-method');

    if (lastDrinkTimeEl) lastDrinkTimeEl.textContent = formatTimestamp(h.last_drink_time);
    if (lastDrinkAgoEl) lastDrinkAgoEl.textContent = formatAgo(h.last_drink_time);
    if (freshnessEl) freshnessEl.textContent = formatAgo(h.last_update);

    if (presenceStateEl) {
        const state = (h.presence_last_state || 'UNKNOWN').toUpperCase();
        presenceStateEl.textContent = state;
        presenceStateEl.className = 'meta-chip-value ' + (state === 'HOME' ? 'presence-home' : (state === 'AWAY' ? 'presence-away' : 'presence-unknown'));
    }
    if (presenceTimeEl) presenceTimeEl.textContent = formatTimestamp(h.presence_last_checked);
    if (presenceMethodEl) {
        presenceMethodEl.textContent = h.presence_last_method || 'none';
        presenceMethodEl.title = h.presence_last_error || '';
    }
}

// --- Data Polling ---
function fetchData() {
    fetch('/api/data')
        .then(response => response.json())
        .then(data => {
            if (data.hydration) {
                const h = data.hydration;
                const weightEl = document.getElementById('hyd-weight');
                const statusEl = document.getElementById('hyd-status');
                const lastDrinkEl = document.getElementById('hyd-last-drink');
                const dailyTotalEl = document.getElementById('hyd-daily-total');
                latestHydrationData = h;

                if (weightEl) weightEl.innerText = h.weight ?? '--';
                if (statusEl) {
                    const stale = (Date.now() / 1000 - (h.last_update || 0)) > 60;
                    statusEl.style.color = stale ? 'var(--text-muted)' : 'var(--success)';
                    statusEl.innerText = (h.status || 'Unknown') + (stale ? ' (Stale)' : '');
                }
                if (lastDrinkEl) {
                    const ml = h.last_drink_ml;
                    lastDrinkEl.innerText = (ml != null && ml > 0) ? ml + ' ml' : (ml === 0 ? '0 ml' : '-- ml');
                }
                if (dailyTotalEl) {
                    const ml = h.daily_total_ml;
                    dailyTotalEl.innerText = (ml != null && ml >= 0) ? ml + ' ml' : '-- ml';
                }
                appendWeightHistory(Number(h.weight));
                renderWeightTrend();
                renderGoal(Number(h.daily_total_ml) || 0);
                renderHydrationMeta(h);
            }
        })
        .catch(err => console.error("Poll Error:", err));
}

// Run polling every 2 seconds
setInterval(fetchData, 2000);

// Request daily total from slave so "Today total" updates (on load and every 60s)
function requestDailyTotal() {
    fetch('/api/hydration/cmd', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ cmd: 'request_daily_total' })
    }).catch(function () {});
}
document.addEventListener('DOMContentLoaded', function () {
    renderGoal(0);
    requestDailyTotal();
    setTimeout(requestDailyTotal, 2000);
});
setInterval(requestDailyTotal, 60000);  // refresh daily total every 60s

// --- Master serial log (data from master) ---
function fetchMasterLog() {
    fetch('/api/master/log?limit=150')
        .then(response => response.json())
        .then(data => {
            const el = document.getElementById('master-log-content');
            if (!el) return;
            const lines = data.lines || [];
            el.textContent = lines.join('\n');
            el.scrollTop = el.scrollHeight;
        })
        .catch(err => console.error('Master log:', err));
}
setInterval(fetchMasterLog, 1500);
document.addEventListener('DOMContentLoaded', fetchMasterLog);

// Initial Call
document.addEventListener('DOMContentLoaded', fetchData);

window.adjustHydrationGoal = adjustHydrationGoal;
