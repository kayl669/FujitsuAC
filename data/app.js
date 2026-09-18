async function api(url, options = {}) {
    const r = await fetch(url, options);
    if (!r.ok) throw new Error(await r.text());
    return r.json().catch(() => ({}))
}

function setText(id, v) {
    const e = document.getElementById(id);
    if (e) e.textContent = v
}

async function refreshStatus() {
    try {
        const s = await api('/api/status');
        const on = s.mode !== 'Off';
        setText('power', s.power);
        setText('subtitle', s.type);
        setText('room', Number(s.room).toFixed(1) + ' °C');
        setText('target', Number(s.target).toFixed(1) + ' °C');
        setText('tempControl', Number(s.target).toFixed(1) + ' °C');
        setText('mode', s.mode);
        setText('fan', s.fan);
        setText('powerW', Number(s.power_w).toFixed(1) + ' W');
        setText('energyKwh', Number(s.energy_kwh).toFixed(3) + ' kWh');
        const m = document.getElementById('modeSelect');
        if (m) m.value = ({
            'Unknown': 'off',
            'Off': 'off',
            'Auto': 'auto',
            'Cool': 'cool',
            'Dry': 'dry',
            'Fan Only': 'fan_only',
            'Heat': 'heat',
        }[s.mode] || 'off');
        const f = document.getElementById('fanSelect');
        if (f) f.value = ({
            'Auto': 'auto',
            'Quiet': 'quiet',
            'Low': 'low',
            'Medium': 'medium',
            'High': 'high'
        }[s.fan] || 'auto');
        const b = document.getElementById('powerBadge');
        if (b) {
            b.className = 'badge ' + (on ? 'on' : 'off');
            b.textContent = on ? 'ON' : 'OFF'
        }
        const c = document.getElementById('connection');
        if (c) {
            c.innerHTML = '<span class="dot"></span>Communication ' + (s.communication ? 'OK' : 'unavailable');
            c.className = 'badge ' + (s.communication ? 'on' : 'off');
        }
    } catch (e) {
        const c = document.getElementById('connection');
        if (c) {
            c.textContent = '<span class="dot"></span>Communication unavailable';
            c.className = 'badge off';
        }
    }
}

async function command(cmd, value = '') {
    await fetch('/action?cmd=' + encodeURIComponent(cmd) + (value ? '&value=' + encodeURIComponent(value) : ''), {method: 'POST'});
    setTimeout(refreshStatus, 500)
}

function bind() {
    document.querySelectorAll('[data-cmd]').forEach(b => b.addEventListener('click', () => command(b.dataset.cmd, b.dataset.value || '')));
    const m = document.getElementById('modeSelect');
    if (m) m.addEventListener('change', () => command('MODE', m.value));
    const f = document.getElementById('fanSelect');
    if (f) f.addEventListener('change', () => command('FAN', f.value))
}

document.addEventListener('DOMContentLoaded', () => {
    bind();
    refreshStatus();
    setInterval(refreshStatus, 3000)
})
