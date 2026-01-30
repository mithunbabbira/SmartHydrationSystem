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
                const weightEl = document.getElementById('hyd-weight');
                const statusEl = document.getElementById('hyd-status');

                if (weightEl) weightEl.innerText = data.hydration.weight;
                if (statusEl) {
                    statusEl.innerText = data.hydration.status;
                    // Optional: Visual cue for active/stale
                    if ((Date.now() / 1000 - data.hydration.last_update) > 60) {
                        statusEl.style.color = 'var(--text-muted)';
                        statusEl.innerText += " (Stale)";
                    } else {
                        statusEl.style.color = 'var(--success)';
                    }
                }
            }
        })
        .catch(err => console.error("Poll Error:", err));
}

// Run polling every 2 seconds
setInterval(fetchData, 2000);

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
