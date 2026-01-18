// Socket.IO Connection
const socket = io();

socket.on('connect', () => {
    console.log('✅ Connected to backend!');
    showToast('Connected to system');
});

socket.on('disconnect', () => {
    console.log('❌ Disconnected from backend!');
    showToast('Connection lost');
    const badge = document.getElementById('system-status');
    badge.querySelector('.label').innerText = 'Reconnecting...';
    badge.querySelector('.pulse').style.background = '#f59e0b';
});

// Constants
const DAILY_GOAL = 2000;
const circle = document.querySelector('.progress-ring__circle');
const radius = circle.r.baseVal.value;
const circumference = radius * 2 * Math.PI;

circle.style.strokeDasharray = `${circumference} ${circumference}`;
circle.style.strokeDashoffset = circumference;

// Chart Instance
let historyChart;

// Initialize
document.addEventListener('DOMContentLoaded', () => {
    updateTime();
    setInterval(updateTime, 1000);
    initHistoryChart();
    fetchInitialStats();
});

function updateTime() {
    const now = new Date();
    document.getElementById('current-time').innerText = now.toLocaleDateString() + ' ' + now.toLocaleTimeString();
}

function fetchInitialStats() {
    console.log('[API] 🔄 Fetching initial stats...');
    fetch('/api/stats')
        .then(res => res.json())
        .then(data => {
            console.log('[API] ✅ Stats received:', data);
            updateConsumption(data.live_ml);
            document.getElementById('session-count').innerText = data.sessions;

            // If we can reach the API, the system is at least partially online
            const badge = document.getElementById('system-status');
            if (badge.querySelector('.label').innerText === 'Connecting...') {
                updateSystemStatus('Online');
            }
        })
        .catch(err => {
            console.error('[API] ❌ Fetch error:', err);
            showToast('Fallback API unreachable');
        });
}

function updateSystemStatus(status) {
    const badge = document.getElementById('system-status');
    const label = badge.querySelector('.label');
    const pulse = badge.querySelector('.pulse');

    label.innerText = status;
    pulse.style.background = (status === 'Online' || status === 'online') ? '#10b981' : '#ef4444';
}

function setProgress(percent) {
    const offset = circumference - (percent / 100) * circumference;
    circle.style.strokeDashoffset = offset;
}

function updateConsumption(ml) {
    document.getElementById('consumption-ml').innerText = Math.round(ml);
    const pct = Math.min(Math.round((ml / DAILY_GOAL) * 100), 100);
    document.getElementById('consumption-pct').innerText = pct + '%';
    setProgress(pct);
}

// ==================== Socket.IO Listeners ====================
socket.on('telemetry_update', (data) => {
    document.getElementById('bottle-weight').innerHTML = `${data.weight.toFixed(1)}<small>g</small>`;

    const deltaEl = document.getElementById('weight-delta');
    deltaEl.innerText = (data.delta >= 0 ? '+' : '') + data.delta.toFixed(1) + 'g';
    deltaEl.style.color = data.delta < -30 ? '#10b981' : (data.delta > 50 ? '#06b6d4' : '#94a3b8');

    document.getElementById('sync-time').innerText = data.last_update;

    const alertEl = document.getElementById('alert-level');
    if (data.alert > 0) {
        alertEl.innerText = 'Alert Phase ' + data.alert;
        alertEl.className = 'status-pill status-alert';
    } else {
        alertEl.innerText = 'Normal';
        alertEl.className = 'status-pill status-ok';
    }
});

socket.on('stats_update', (data) => {
    updateConsumption(data.today_ml);
    // Refresh session count after a drink
    fetchInitialStats();
});

socket.on('status_update', (data) => {
    const badge = document.getElementById('system-status');
    const label = badge.querySelector('.label');
    const pulse = badge.querySelector('.pulse');

    label.innerText = data.status.charAt(0).toUpperCase() + data.status.slice(1);
    pulse.style.background = data.status === 'online' ? '#10b981' : '#ef4444';
});

// ==================== Commands ====================
function sendCommand(cmd, val) {
    fetch('/api/command', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ command: cmd, value: val })
    }).then(res => res.json())
        .then(data => showToast('Command Sent: ' + cmd));
}

function confirmReset() {
    if (confirm('Are you sure you want to reset today\'s water consumption? This clears the ESP32 and Database.')) {
        sendCommand('reset_today', 'execute');
    }
}

function confirmReboot() {
    if (confirm('Reboot the ESP32 bottle base?')) {
        sendCommand('reboot', 'execute');
    }
}

function showToast(msg) {
    const container = document.getElementById('toast-container');
    const toast = document.createElement('div');
    toast.className = 'toast glass';
    toast.innerText = msg;
    container.appendChild(toast);
    setTimeout(() => toast.remove(), 3000);
}

// ==================== Charts ====================
function initHistoryChart() {
    const ctx = document.getElementById('history-chart').getContext('2d');
    historyChart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: Array.from({ length: 24 }, (_, i) => i + ':00'),
            datasets: [{
                label: 'Consumption (ml)',
                data: Array(24).fill(0),
                borderColor: '#3b82f6',
                backgroundColor: 'rgba(59, 130, 246, 0.1)',
                fill: true,
                tension: 0.4
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: { legend: { display: false } },
            scales: {
                y: { grid: { color: 'rgba(255,255,255,0.05)' }, ticks: { color: '#94a3b8' } },
                x: { grid: { display: false }, ticks: { color: '#94a3b8' } }
            }
        }
    });
}
