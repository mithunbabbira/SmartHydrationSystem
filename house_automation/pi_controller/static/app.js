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
