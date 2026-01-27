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

function toggleMaster(checkbox) {
    const action = checkbox.checked ? 'on' : 'off';
    apiCall('/api/master/cmd', { action: action });
    // Optimistic Update for UI? Or waiting for confirmation?
    // For now, fire and forget.
}
