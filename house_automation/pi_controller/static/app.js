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
    apiCall('/api/hydration/cmd', { cmd: cmd });
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
