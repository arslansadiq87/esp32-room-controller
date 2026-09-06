// Room Controller Web Application Logic
let ws = null;
let reconnectTimer = null;

// Fan Dimmer State Cache
const dimmerState = {
    1: { level: 50, state: false, isDragging: false },
    2: { level: 50, state: false, isDragging: false }
};

// Relay Friendly Names & Hardware Mapping (Relays 1..6 -> GPB2..GPB7 / ULN2803A Pins 16..11)
const RELAY_LABELS = [
    { name: "Main Light 1", sub: "Relay 1 (GPB2 • Pin 16)", theme: "theme-blue" },
    { name: "Main Light 2", sub: "Relay 2 (GPB3 • Pin 15)", theme: "theme-blue" },
    { name: "Spotlights",   sub: "Relay 3 (GPB4 • Pin 14)", theme: "theme-green" },
    { name: "Wall Socket 1",sub: "Relay 4 (GPB5 • Pin 13)", theme: "theme-purple" },
    { name: "Wall Socket 2",sub: "Relay 5 (GPB6 • Pin 12)", theme: "theme-purple" },
    { name: "Ambient Strip",sub: "Relay 6 (GPB7 • Pin 11)", theme: "theme-amber" }
];

// Channel Custom Display Names (Loaded from NVS & LocalStorage)
let channelNames = {
    fan1: "Fan 1",
    fan2: "Fan 2",
    relay1: "Main Light 1",
    relay2: "Main Light 2",
    relay3: "Spotlights",
    relay4: "Wall Socket 1",
    relay5: "Wall Socket 2",
    relay6: "Ambient Strip"
};

function loadStoredChannelNames() {
    try {
        const saved = localStorage.getItem('rc_channel_names');
        if (saved) {
            const parsed = JSON.parse(saved);
            channelNames = Object.assign({}, channelNames, parsed);
        }
    } catch (e) {}
}

async function fetchChannelNames() {
    try {
        const res = await fetch('/api/names');
        if (res.ok) {
            const data = await res.json();
            channelNames = Object.assign({}, channelNames, data);
            try { localStorage.setItem('rc_channel_names', JSON.stringify(channelNames)); } catch (e) {}
            applyChannelNamesToUI();
        }
    } catch (e) {}
}

function applyChannelNamesToUI() {
    // Fan Titles
    const fan1El = document.getElementById('fanTitle1');
    if (fan1El) fan1El.innerText = channelNames.fan1 || "Fan 1";
    const fan2El = document.getElementById('fanTitle2');
    if (fan2El) fan2El.innerText = channelNames.fan2 || "Fan 2";

    // Relay Titles on Dashboard
    for (let i = 1; i <= 6; i++) {
        const rEl = document.getElementById(`relayName${i}`);
        if (rEl) rEl.innerText = channelNames[`relay${i}`] || RELAY_LABELS[i-1].name;
    }

    // Automation Switch Titles
    const autoFan1 = document.querySelector('#autoFanCard1 .auto-switch-name');
    if (autoFan1) autoFan1.innerText = `${channelNames.fan1 || "Fan 1"} Controller`;
    const autoFan2 = document.querySelector('#autoFanCard2 .auto-switch-name');
    if (autoFan2) autoFan2.innerText = `${channelNames.fan2 || "Fan 2"} Controller`;

    for (let i = 1; i <= 6; i++) {
        const autoRelay = document.querySelector(`#autoRelayCard${i} .auto-switch-name`);
        if (autoRelay) autoRelay.innerText = channelNames[`relay${i}`] || RELAY_LABELS[i-1].name;
    }

    // Fill Settings Input Fields
    const f1In = document.getElementById('nameInputFan1');
    if (f1In) f1In.value = channelNames.fan1 || "Fan 1";
    const f2In = document.getElementById('nameInputFan2');
    if (f2In) f2In.value = channelNames.fan2 || "Fan 2";

    for (let i = 1; i <= 6; i++) {
        const rIn = document.getElementById(`nameInputRelay${i}`);
        if (rIn) rIn.value = channelNames[`relay${i}`] || RELAY_LABELS[i-1].name;
    }
}

async function saveChannelNames() {
    const statusEl = document.getElementById('nameSaveStatus');
    const f1 = document.getElementById('nameInputFan1')?.value.trim() || "Fan 1";
    const f2 = document.getElementById('nameInputFan2')?.value.trim() || "Fan 2";
    const r1 = document.getElementById('nameInputRelay1')?.value.trim() || "Main Light 1";
    const r2 = document.getElementById('nameInputRelay2')?.value.trim() || "Main Light 2";
    const r3 = document.getElementById('nameInputRelay3')?.value.trim() || "Spotlights";
    const r4 = document.getElementById('nameInputRelay4')?.value.trim() || "Wall Socket 1";
    const r5 = document.getElementById('nameInputRelay5')?.value.trim() || "Wall Socket 2";
    const r6 = document.getElementById('nameInputRelay6')?.value.trim() || "Ambient Strip";

    channelNames = {
        fan1: f1,
        fan2: f2,
        relay1: r1,
        relay2: r2,
        relay3: r3,
        relay4: r4,
        relay5: r5,
        relay6: r6
    };

    try { localStorage.setItem('rc_channel_names', JSON.stringify(channelNames)); } catch (e) {}
    applyChannelNamesToUI();

    if (statusEl) {
        statusEl.className = 'save-status';
        statusEl.innerText = 'Saving...';
    }

    try {
        const res = await fetch('/api/names', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(channelNames)
        });
        if (res.ok) {
            if (statusEl) {
                statusEl.innerText = 'Names saved & synced!';
                setTimeout(() => { if (statusEl) statusEl.innerText = ''; }, 3000);
            }
        } else {
            throw new Error('Save failed');
        }
    } catch (e) {
        if (statusEl) {
            statusEl.innerText = 'Saved locally!';
            setTimeout(() => { if (statusEl) statusEl.innerText = ''; }, 3000);
        }
    }
}

// Dashboard Card Order State & Logic
let cardOrder = ['relays', 'fans', 'env', 'power'];
let isSavingLayout = false;

function loadStoredCardOrder() {
    try {
        const saved = localStorage.getItem('rc_card_order');
        if (saved) {
            const parsed = JSON.parse(saved);
            if (Array.isArray(parsed) && parsed.length === 4) {
                cardOrder = parsed;
                applyCardOrderToUI(cardOrder);
            }
        }
    } catch (e) {}
}

async function fetchCardOrder() {
    try {
        const res = await fetch('/api/layout');
        if (res.ok) {
            const data = await res.json();
            if (data.order && Array.isArray(data.order) && data.order.length === 4) {
                cardOrder = data.order;
                try { localStorage.setItem('rc_card_order', JSON.stringify(cardOrder)); } catch (e) {}
                applyCardOrderToUI(cardOrder);
            }
        }
    } catch (e) {}
}

function applyCardOrderToUI(orderArray) {
    if (orderArray && Array.isArray(orderArray) && orderArray.length === 4) {
        cardOrder = orderArray;
    }

    const container = document.querySelector('.app-container');
    const footer = document.querySelector('.footer');

    const idMap = {
        'relays': document.getElementById('secRelays'),
        'fans': document.getElementById('secFans'),
        'env': document.getElementById('secEnv'),
        'power': document.getElementById('secPower')
    };

    if (container) {
        cardOrder.forEach((key, index) => {
            const el = idMap[key];
            if (el) {
                el.style.order = String(index + 1);
                if (footer) {
                    container.insertBefore(el, footer);
                } else {
                    container.appendChild(el);
                }
            }
        });
    }

    updateCardOrderInputs();
}

function updateCardOrderInputs() {
    const list = document.getElementById('cardOrderList');
    if (list) {
        cardOrder.forEach(key => {
            const row = document.getElementById(`orderItem_${key}`);
            if (row) list.appendChild(row);
        });
    }

    cardOrder.forEach((key, index) => {
        const posEl = document.getElementById(`posNum_${key}`);
        if (posEl) posEl.innerText = `#${index + 1}`;

        const row = document.getElementById(`orderItem_${key}`);
        if (row) {
            const upBtn = row.querySelector('.btn-order-up');
            const downBtn = row.querySelector('.btn-order-down');
            if (upBtn) upBtn.disabled = (index === 0);
            if (downBtn) downBtn.disabled = (index === cardOrder.length - 1);
        }
    });
}

async function moveCardOrder(key, delta) {
    const currentIdx = cardOrder.indexOf(key);
    if (currentIdx === -1) return;
    const targetIdx = currentIdx + delta;
    if (targetIdx < 0 || targetIdx >= cardOrder.length) return;

    // Swap positions
    const temp = cardOrder[targetIdx];
    cardOrder[targetIdx] = key;
    cardOrder[currentIdx] = temp;

    // 1. Instantly update UI on dashboard and in settings modal
    applyCardOrderToUI(cardOrder);

    // 2. Instantly persist to localStorage
    try { 
        localStorage.setItem('rc_card_order', JSON.stringify(cardOrder)); 
    } catch (e) {}

    // 3. Immediately auto-save to ESP32 backend via REST
    const statusEl = document.getElementById('orderSaveStatus');
    if (statusEl) {
        statusEl.className = 'save-status';
        statusEl.innerText = 'Saving...';
    }

    isSavingLayout = true;
    try {
        const res = await fetch('/api/layout', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ order: cardOrder })
        });
        if (res.ok) {
            if (statusEl) {
                statusEl.innerText = 'Saved!';
                setTimeout(() => { if (statusEl) statusEl.innerText = ''; }, 2000);
            }
        }
    } catch (e) {
        console.error('Failed to sync layout to ESP32:', e);
        if (statusEl) {
            statusEl.innerText = 'Saved locally!';
            setTimeout(() => { if (statusEl) statusEl.innerText = ''; }, 2000);
        }
    } finally {
        setTimeout(() => { isSavingLayout = false; }, 2000);
    }
}

// Initialize UI
document.addEventListener('DOMContentLoaded', () => {
    loadStoredChannelNames();
    loadStoredCardOrder();
    initRelayGrid();
    initRotaryDials();
    initAutomationSwitchesGrid();
    initOtaDropZone();
    initWebSocket();
    fetchLedSettings();
    initPowerChart();
    fetchPowerHistory();
    fetchChannelNames();
    fetchCardOrder();
    fetchMqttConfig();
});

// Render 6-Relay Grid (Strictly 6 Channels, Subtext Removed)
function initRelayGrid() {
    const container = document.getElementById('relaysContainer');
    if (!container) return;
    container.innerHTML = '';

    for (let i = 1; i <= RELAY_LABELS.length; i++) {
        const info = RELAY_LABELS[i - 1];
        const displayName = channelNames[`relay${i}`] || info.name;
        const card = document.createElement('div');
        card.className = `relay-card ${info.theme}`;
        card.id = `relayCard${i}`;
        card.onclick = () => toggleRelay(i);

        card.innerHTML = `
            <div class="relay-card-left">
                <div class="hex-badge hex-relay">
                    <svg class="hex-frame" width="46" height="46" viewBox="0 0 64 64">
                        <polygon points="32,4 58,19 58,45 32,60 6,45 6,19" fill="none" stroke="currentColor" stroke-width="1.8" stroke-opacity="0.35"/>
                        <polygon points="32,8 54,21 54,43 32,56 10,43 10,21" fill="rgba(255,255,255,0.02)" stroke="currentColor" stroke-width="2"/>
                    </svg>
                    <div class="hex-icon-inner">
                        <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round">
                            <path d="M18.36 6.64a9 9 0 1 1-12.73 0"/>
                            <line x1="12" y1="2" x2="12" y2="12"/>
                        </svg>
                    </div>
                </div>
                <div class="relay-card-text">
                    <div class="relay-title-row">
                        <span class="relay-name" id="relayName${i}">${displayName}</span>
                        <span class="relay-auto-badge" id="relayAutoTag${i}" style="display:none;">AUTO</span>
                    </div>
                </div>
            </div>
            <div class="cyber-switch">
                <span class="cyber-switch-thumb"></span>
            </div>
        `;
        container.appendChild(card);
    }
}

// Interactive Rotary Dials for Fan 1 and Fan 2
function initRotaryDials() {
    [1, 2].forEach(id => {
        const wrapper = document.getElementById(`rotaryWrapper${id}`);
        if (!wrapper) return;

        let lastSendTime = 0;

        function handlePointerMove(e) {
            if (!dimmerState[id].isDragging) return;
            e.preventDefault();

            const clientX = e.touches ? e.touches[0].clientX : e.clientX;
            const clientY = e.touches ? e.touches[0].clientY : e.clientY;

            const rect = wrapper.getBoundingClientRect();
            const centerX = rect.left + rect.width / 2;
            const centerY = rect.top + rect.height / 2;

            const dx = clientX - centerX;
            const dy = clientY - centerY;

            // Calculate angle in degrees
            let angle = Math.atan2(dy, dx) * (180 / Math.PI);
            let angleNorm = (angle + 360) % 360;

            // 240 deg Arc runs from 150 deg (0%) over 270 deg (50% top) to 390/30 deg (100%)
            let delta = (angleNorm - 150 + 360) % 360;
            let level = 0;

            if (delta <= 240) {
                level = Math.round((delta / 240) * 100);
            } else {
                // In dead-band (bottom 120 deg)
                level = (delta < 300) ? 100 : 0;
            }

            level = Math.max(0, Math.min(100, level));
            dimmerState[id].level = level;
            dimmerState[id].state = (level > 0);

            updateDimmerUI(id, level, dimmerState[id].state);

            // Throttled WebSocket update during drag (every 80ms)
            const now = Date.now();
            if (now - lastSendTime > 80) {
                lastSendTime = now;
                sendWsMessage({
                    type: 'dimmer',
                    id: id,
                    level: level,
                    state: dimmerState[id].state
                });
            }
        }

        function handlePointerUp() {
            if (dimmerState[id].isDragging) {
                dimmerState[id].isDragging = false;
                // Send final confirmed state
                sendWsMessage({
                    type: 'dimmer',
                    id: id,
                    level: dimmerState[id].level,
                    state: dimmerState[id].state
                });
            }
        }

        // Mouse events
        wrapper.addEventListener('mousedown', (e) => {
            if (fanAnimFrames[id]) {
                cancelAnimationFrame(fanAnimFrames[id]);
                fanAnimFrames[id] = null;
            }
            dimmerState[id].isDragging = true;
            handlePointerMove(e);
        });

        window.addEventListener('mousemove', handlePointerMove);
        window.addEventListener('mouseup', handlePointerUp);

        // Touch events
        wrapper.addEventListener('touchstart', (e) => {
            if (fanAnimFrames[id]) {
                cancelAnimationFrame(fanAnimFrames[id]);
                fanAnimFrames[id] = null;
            }
            dimmerState[id].isDragging = true;
            handlePointerMove(e);
        }, { passive: false });

        window.addEventListener('touchmove', handlePointerMove, { passive: false });
        window.addEventListener('touchend', handlePointerUp);

        // Mouse wheel adjustment
        wrapper.addEventListener('wheel', (e) => {
            if (fanAnimFrames[id]) {
                cancelAnimationFrame(fanAnimFrames[id]);
                fanAnimFrames[id] = null;
            }
            e.preventDefault();
            let newLevel = dimmerState[id].level + (e.deltaY < 0 ? 5 : -5);
            newLevel = Math.max(0, Math.min(100, newLevel));
            dimmerState[id].level = newLevel;
            dimmerState[id].state = (newLevel > 0);
            updateDimmerUI(id, newLevel, dimmerState[id].state);
            sendWsMessage({
                type: 'dimmer',
                id: id,
                level: newLevel,
                state: dimmerState[id].state
            });
        }, { passive: false });
    });
}

// Update Connection Status Pill in Top Header
function updateWsBadge(connected) {
    const badge = document.getElementById('wsStatusBadge');
    const text = document.getElementById('wsStatusText');
    if (!badge || !text) return;
    if (connected) {
        badge.classList.remove('offline');
        badge.classList.add('connected');
        text.innerText = 'Connected';
    } else {
        badge.classList.remove('connected');
        badge.classList.add('offline');
        text.innerText = 'Offline';
    }
}

// WebSocket Connection & Real-Time Sync
function initWebSocket() {
    if (ws) {
        ws.close();
    }

    const wsUrl = `ws://${window.location.host}/ws`;
    ws = new WebSocket(wsUrl);

    ws.onopen = () => {
        console.log('[WS] Connected');
        updateWsBadge(true);
        if (reconnectTimer) {
            clearTimeout(reconnectTimer);
            reconnectTimer = null;
        }
        // Request initial status
        sendWsMessage({ type: 'get_status' });
        const now = new Date();
        sendWsMessage({ type: 'sync_time', hour: now.getHours(), minute: now.getMinutes() });
        sendWsMessage({ type: 'automation_get' });
    };

    ws.onclose = () => {
        console.log('[WS] Disconnected, scheduling reconnect...');
        updateWsBadge(false);
        if (!reconnectTimer) {
            reconnectTimer = setTimeout(initWebSocket, 2000);
        }
    };

    ws.onerror = (err) => {
        console.error('[WS] Error:', err);
        updateWsBadge(false);
    };

    ws.onmessage = (event) => {
        try {
            const data = JSON.parse(event.data);
            handleIncomingMessage(data);
        } catch (e) {
            console.error('[WS] Failed to parse message:', e, event.data);
        }
    };
}

function sendWsMessage(obj) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify(obj));
    }
}

function updateWsBadge(connected) {
    const badge = document.getElementById('wsStatusBadge');
    const text = document.getElementById('wsStatusText');
    if (connected) {
        badge.classList.add('connected');
        text.innerText = 'Connected';
    } else {
        badge.classList.remove('connected');
        text.innerText = 'Disconnected';
    }
}

// Process Real-time updates
function handleIncomingMessage(msg) {
    switch (msg.type) {
        case 'full_status':
            // Update 6 Relays
            if (msg.relays) {
                msg.relays.forEach(r => updateRelayUI(r.id, r.state));
            }
            // Update Fans / Dimmers
            if (msg.dimmers) {
                msg.dimmers.forEach(d => {
                    if (!dimmerState[d.id] || !dimmerState[d.id].isDragging) {
                        updateDimmerUI(d.id, d.level, d.state);
                    }
                });
            }
            // Update Power Telemetry
            if (msg.power) {
                updateTelemetryUI(msg.power);
            }
            // Update Environmental Telemetry (STCC4)
            if (msg.environment) {
                updateEnvironmentUI(msg.environment);
            }
            // Update Presence Telemetry (C4002 mmWave)
            if (msg.presence) {
                updatePresenceUI(msg.presence);
            }
            // Update System stats
            if (msg.system) {
                updateSystemUI(msg.system);
                if (msg.system.ip) {
                    const ipEl = document.getElementById('wifiCurIp');
                    if (ipEl) ipEl.innerText = msg.system.ip;
                }
            }
            // Update Home Assistant / MQTT status
            if (msg.mqtt) {
                updateMqttStatusBadge(msg.mqtt);
            }
            // Update Custom Channel Names
            if (msg.names) {
                channelNames = Object.assign({}, channelNames, msg.names);
                try { localStorage.setItem('rc_channel_names', JSON.stringify(channelNames)); } catch (e) {}
                applyChannelNamesToUI();
            }
            // Update Dashboard Card Order
            if (msg.card_order && !isSavingLayout) {
                const isSame = Array.isArray(msg.card_order) && 
                    msg.card_order.length === cardOrder.length && 
                    msg.card_order.every((val, i) => val === cardOrder[i]);
                if (!isSame) {
                    applyCardOrderToUI(msg.card_order);
                }
            }
            // Update Power Logging & Consumption stats
            if (msg.power_logging !== undefined) {
                if (powerLoggingEnabled !== !!msg.power_logging) {
                    powerLoggingEnabled = !!msg.power_logging;
                    updatePowerLoggingUI();
                    if (powerLoggingEnabled) fetchPowerHistory();
                }
            }
            if (msg.daily_kwh !== undefined) {
                const el = document.getElementById('valDailyKwh');
                if (el) el.innerText = Number(msg.daily_kwh).toFixed(3);
            }
            if (msg.monthly_kwh !== undefined) {
                const el = document.getElementById('valMonthlyKwh');
                if (el) el.innerText = Number(msg.monthly_kwh).toFixed(3);
            }
            if (msg.peak_power !== undefined) {
                const el = document.getElementById('valPeakPower');
                if (el) el.innerText = Number(msg.peak_power).toFixed(1);
            }
            if (msg.avg_power !== undefined) {
                const el = document.getElementById('valAvgPower');
                if (el) el.innerText = Number(msg.avg_power).toFixed(1);
            }
            break;

        case 'automation_config':
            updateAutomationUI(msg);
            break;

        case 'safety_config':
            updateSafetyUI(msg);
            break;

        case 'relay_update':
            updateRelayUI(msg.id, msg.state);
            break;

        case 'dimmer_update':
            if (!dimmerState[msg.id] || !dimmerState[msg.id].isDragging) {
                updateDimmerUI(msg.id, msg.level, msg.state);
            }
            break;

        case 'telemetry':
            updateTelemetryUI(msg);
            updatePresenceFromTelemetry(msg);
            updateEnvFromTelemetry(msg);
            updateLiveAutomationTelemetry(msg);
            if (msg.uptime !== undefined) updateUptimeUI(msg.uptime);
            if (msg.rssi !== undefined) updateRssiUI(msg.rssi);
            if (msg.radar_status_led !== undefined) ledStates.radar_status = !!msg.radar_status_led;
            if (msg.radar_output_led !== undefined) ledStates.radar_output = !!msg.radar_output_led;
            if (msg.system_status_led !== undefined) ledStates.controller = !!msg.system_status_led;
            updateLedControlsUI();
            if (msg.power_logging !== undefined) {
                if (powerLoggingEnabled !== !!msg.power_logging) {
                    powerLoggingEnabled = !!msg.power_logging;
                    updatePowerLoggingUI();
                    if (powerLoggingEnabled) fetchPowerHistory();
                }
            }
            if (msg.daily_kwh !== undefined) {
                const el = document.getElementById('valDailyKwh');
                if (el) el.innerText = Number(msg.daily_kwh).toFixed(3);
            }
            if (msg.monthly_kwh !== undefined) {
                const el = document.getElementById('valMonthlyKwh');
                if (el) el.innerText = Number(msg.monthly_kwh).toFixed(3);
            }
            if (msg.peak_power !== undefined) {
                const el = document.getElementById('valPeakPower');
                if (el) el.innerText = Number(msg.peak_power).toFixed(1);
            }
            if (msg.avg_power !== undefined) {
                const el = document.getElementById('valAvgPower');
                if (el) el.innerText = Number(msg.avg_power).toFixed(1);
            }
            break;
    }
}

// UI Updaters
function updateRelayUI(id, state) {
    const card = document.getElementById(`relayCard${id}`);
    if (card) {
        if (state) {
            card.classList.add('active');
        } else {
            card.classList.remove('active');
        }
    }
}

function updateDimmerUI(id, level, state) {
    dimmerState[id].level = level;
    dimmerState[id].state = state;

    const card = document.getElementById(`dimmerCard${id}`);
    const track = document.getElementById(`rotaryTrack${id}`);
    const dot = document.getElementById(`rotaryDot${id}`);
    const valText = document.getElementById(`dimmerVal${id}`);
    const statusPill = document.getElementById(`fanStatusPill${id}`);
    const toggleBtn = document.getElementById(`dimmerToggle${id}`);
    const bladeIcon = document.getElementById(`fanBladeIcon${id}`);
    const centerIcon = document.getElementById(`fanCenterIcon${id}`);

    // Update Percentage Text
    if (valText) valText.innerText = Math.round(level);

    // Update Progress Arc Track (pathLength="100", 240 deg arc)
    if (track) {
        const offset = state ? (100 - level) : 100;
        track.style.strokeDashoffset = offset;
        track.style.opacity = (state && level > 0) ? '1' : '0.35';
    }

    // Update Rotary Knob Orb Indicator Dot:
    // Center is (110, 110), Radius = 80, sweep from 150 deg to 390 deg (240 deg total)
    if (dot) {
        const angleDeg = 150 + (level / 100) * 240;
        const angleRad = angleDeg * (Math.PI / 180);
        const cx = 110 + 80 * Math.cos(angleRad);
        const cy = 110 + 80 * Math.sin(angleRad);
        dot.setAttribute('cx', cx.toFixed(2));
        dot.setAttribute('cy', cy.toFixed(2));
    }

    // Update Power Button (dim when 0% or OFF, active cyan glow when > 0%)
    if (toggleBtn) {
        toggleBtn.classList.toggle('active', state && level > 0);
    }

    // Update Status Pill (OFF / ON)
    if (statusPill) {
        if (!state || level === 0) {
            statusPill.innerText = 'OFF';
            statusPill.classList.remove('active');
        } else {
            statusPill.innerText = 'ON';
            statusPill.classList.add('active');
        }
    }

    // Spin Fan Propeller Icon when active
    const isSpinning = (state && level > 0);
    if (bladeIcon) bladeIcon.classList.toggle('fan-spin-fast', isSpinning);
    if (centerIcon) centerIcon.classList.toggle('fan-spin-fast', isSpinning);

    // Update Card Active Glow
    if (card) {
        card.classList.toggle('active', isSpinning);
    }

    // Update 5 Preset Buttons (OFF, 25%, 50%, 75%, MAX) via continuous triangular cross-fade
    updateFanPresetsCrossFade(id, level, state);
}

// Continuous Linear Interpolation (Cross-fade) for Fan Preset Buttons
function updateFanPresetsCrossFade(id, level, state) {
    const effectiveSpeed = (state && level > 0) ? Number(level) : 0;
    const presets = [
        { target: 0, el: document.getElementById(`fanPreset${id}_0`) },
        { target: 25, el: document.getElementById(`fanPreset${id}_25`) },
        { target: 50, el: document.getElementById(`fanPreset${id}_50`) },
        { target: 75, el: document.getElementById(`fanPreset${id}_75`) },
        { target: 100, el: document.getElementById(`fanPreset${id}_100`) }
    ];

    const S = 25; // Interval step between adjacent presets

    presets.forEach(({ target, el }) => {
        if (!el) return;
        const distance = Math.abs(effectiveSpeed - target);
        // Triangular interpolation function: weight = max(0, 1 - distance / S)
        const weight = Math.max(0, Math.min(1, 1 - (distance / S)));

        // 1. Set CSS Custom Property for background pill opacity
        el.style.setProperty('--active-weight', weight.toFixed(3));

        // 2. Scaled outer glow / box shadow: 0 0 16px rgba(0, 180, 255, 0.65 * weight)
        if (weight > 0.01) {
            const glowPx = Math.round(16 * weight);
            const glowAlpha = (0.65 * weight).toFixed(3);
            const borderAlpha = (0.85 * weight).toFixed(3);
            el.style.boxShadow = `0 0 ${glowPx}px rgba(0, 180, 255, ${glowAlpha})`;
            el.style.borderColor = `rgba(0, 212, 255, ${borderAlpha})`;
            el.style.transform = `scale(${(1 + weight * 0.025).toFixed(3)})`;
        } else {
            el.style.boxShadow = 'none';
            el.style.borderColor = 'transparent';
            el.style.transform = 'scale(1)';
        }

        // 3. Smooth text color interpolation from muted slate (#8899a6: 136, 153, 166) to crisp white (#ffffff: 255, 255, 255)
        const r = Math.round(136 + (255 - 136) * weight);
        const g = Math.round(153 + (255 - 153) * weight);
        const b = Math.round(166 + (255 - 166) * weight);
        el.style.color = `rgb(${r}, ${g}, ${b})`;
        el.style.fontWeight = weight > 0.45 ? '700' : '600';

        // Keep active class for semantic state
        el.classList.toggle('active', weight >= 0.85);
    });
}

// Animation controller for smooth preset click & power toggle transitions
let fanAnimFrames = { 1: null, 2: null };

function animateFanToLevel(id, targetLevel, onComplete) {
    if (fanAnimFrames[id]) {
        cancelAnimationFrame(fanAnimFrames[id]);
        fanAnimFrames[id] = null;
    }

    const startLevel = (dimmerState[id].state && dimmerState[id].level > 0) ? Number(dimmerState[id].level) : 0;
    const endLevel = Math.max(0, Math.min(100, Math.round(targetLevel)));

    if (Math.abs(startLevel - endLevel) < 0.5) {
        dimmerState[id].level = endLevel;
        dimmerState[id].state = (endLevel > 0);
        updateDimmerUI(id, endLevel, dimmerState[id].state);
        if (onComplete) onComplete();
        return;
    }

    const startTime = performance.now();
    const distance = Math.abs(endLevel - startLevel);
    const duration = Math.min(320, Math.max(160, distance * 3.2));

    function step(currentTime) {
        const elapsed = currentTime - startTime;
        const progress = Math.min(1, elapsed / duration);
        // Smooth cubic ease-out
        const ease = 1 - Math.pow(1 - progress, 3);
        const currentLevel = startLevel + (endLevel - startLevel) * ease;
        const currentState = (endLevel > 0 || currentLevel > 0.5);

        updateDimmerUI(id, currentLevel, currentState);

        if (progress < 1) {
            fanAnimFrames[id] = requestAnimationFrame(step);
        } else {
            fanAnimFrames[id] = null;
            dimmerState[id].level = endLevel;
            dimmerState[id].state = (endLevel > 0);
            updateDimmerUI(id, endLevel, dimmerState[id].state);
            if (onComplete) onComplete();
        }
    }

    fanAnimFrames[id] = requestAnimationFrame(step);
}

// Fan Preset Pill Click with smooth dial sweep & button cross-fade
function setFanPreset(id, level) {
    const targetLevel = Math.max(0, Math.min(100, Number(level) || 0));
    const newState = (targetLevel > 0);

    dimmerState[id].isDragging = false;

    sendWsMessage({
        type: 'dimmer',
        id: id,
        level: targetLevel,
        state: newState
    });

    animateFanToLevel(id, targetLevel);
}

function updateTelemetryUI(pwr) {
    if (!pwr) return;

    const chip = document.getElementById('acPowerChip');
    const vEl = document.getElementById('valVoltage');
    const iEl = document.getElementById('valCurrent');
    const pEl = document.getElementById('valPower');
    const appEl = document.getElementById('valApparent');
    const pfEl = document.getElementById('valPf');
    const freqEl = document.getElementById('valFreq');
    const curSubEl = document.getElementById('valCurrentSub');

    // Online & AC State evaluation
    const isOnline = (pwr.online !== undefined) ? !!pwr.online : (pwr.pwr_online !== undefined ? !!pwr.pwr_online : true);
    let acState = pwr.ac_state;
    if (!acState) {
        if (!isOnline) {
            acState = 'offline';
        } else if (pwr.ac_present === true) {
            acState = 'present';
        } else if (pwr.ac_present === false) {
            acState = 'idle';
        } else {
            acState = (pwr.v_rms !== undefined && pwr.v_rms >= 20.0) ? 'present' : 'idle';
        }
    }

    // 1. Dynamic Status Chip Badge
    if (chip) {
        chip.className = 'ac-power-chip';
        if (!isOnline || acState === 'offline') {
            chip.classList.add('ac-chip-offline');
            chip.innerText = 'ACS37800 Offline';
        } else if (acState === 'checking') {
            chip.classList.add('ac-chip-checking');
            chip.innerText = 'Checking AC Input...';
        } else if (acState === 'present' || pwr.ac_present === true) {
            chip.classList.add('ac-chip-active');
            chip.innerText = 'ACS37800 Online • AC Active';
        } else {
            chip.classList.add('ac-chip-idle');
            chip.innerText = 'ACS37800 Online • AC Input Idle';
        }
    }

    // 2. Metrics Conditioning
    if (!isOnline || acState === 'offline') {
        // Case 3: IC Communication Failed
        if (vEl) vEl.innerText = '--';
        if (iEl) iEl.innerText = '--';
        if (pEl) pEl.innerText = '--';
        if (appEl) appEl.innerText = '-- VA Apparent';
        if (pfEl) pfEl.innerText = '--';
        if (freqEl) freqEl.innerText = 'IC Offline';
        if (curSubEl) curSubEl.innerText = 'Sensor Offline';
    } else if (acState === 'checking') {
        // Startup stabilization (~4 seconds)
        if (vEl) vEl.innerText = '--';
        if (iEl) iEl.innerText = '--';
        if (pEl) pEl.innerText = '0.0';
        if (appEl) appEl.innerText = '0.0 VA Apparent';
        if (pfEl) pfEl.innerText = '0.00';
        if (freqEl) freqEl.innerText = 'Checking AC...';
        if (curSubEl) curSubEl.innerText = 'Stabilizing Filter';
    } else if (acState === 'present' || pwr.ac_present === true) {
        // Case 1: IC Online + AC Connected
        if (vEl && pwr.v_rms !== undefined && pwr.v_rms >= 0) vEl.innerText = pwr.v_rms.toFixed(1);
        if (iEl && pwr.i_rms !== undefined && pwr.i_rms >= 0) iEl.innerText = pwr.i_rms.toFixed(2);
        if (pEl && pwr.p_active !== undefined) pEl.innerText = Math.max(0, pwr.p_active).toFixed(1);
        if (appEl && pwr.p_apparent !== undefined) appEl.innerText = `${Math.max(0, pwr.p_apparent).toFixed(1)} VA Apparent`;
        if (pfEl && pwr.pf !== undefined) pfEl.innerText = pwr.pf.toFixed(2);
        if (freqEl && pwr.freq !== undefined && pwr.freq > 0) freqEl.innerText = `${pwr.freq.toFixed(1)} Hz`;
        else if (freqEl) freqEl.innerText = '50.0 Hz';
        if (curSubEl) curSubEl.innerText = 'Real-time load';
    } else {
        // Case 2: IC Connected + No AC Input (ADC floating noise suppressed)
        if (vEl) vEl.innerText = '--';
        if (iEl) iEl.innerText = '--';
        if (pEl) pEl.innerText = '0.0';
        if (appEl) appEl.innerText = '0.0 VA Apparent';
        if (pfEl) pfEl.innerText = '0.00';
        if (freqEl) freqEl.innerText = 'AC Input Not Detected';
        if (curSubEl) curSubEl.innerText = 'AC Input Idle';
    }
}

function updateEnvironmentUI(env) {
    if (env.co2_ppm !== undefined) {
        document.getElementById('valCo2').innerText = env.co2_ppm;
        const qEl = document.getElementById('valAirQuality');
        if (qEl) {
            qEl.className = 'tel-sub';
            if (env.co2_ppm < 800) {
                qEl.innerText = 'Air Quality: Excellent';
                qEl.classList.add('air-good');
            } else if (env.co2_ppm < 1200) {
                qEl.innerText = 'Air Quality: Moderate';
                qEl.classList.add('air-warn');
            } else {
                qEl.innerText = 'Air Quality: High CO₂';
                qEl.classList.add('air-bad');
            }
        }
    }
    if (env.temp !== undefined) document.getElementById('valTemp').innerText = env.temp.toFixed(1);
    if (env.humidity !== undefined) document.getElementById('valHumidity').innerText = `Humidity: ${env.humidity.toFixed(1)}% RH`;
}

function updateEnvFromTelemetry(msg) {
    if (msg.co2_ppm !== undefined) {
        updateEnvironmentUI({
            co2_ppm: msg.co2_ppm,
            temp: msg.temp,
            humidity: msg.humidity
        });
    }
}

function updatePresenceUI(pres) {
    const card = document.getElementById('cardPresence');
    const stateEl = document.getElementById('valPresenceState');
    const distEl = document.getElementById('valPresenceDist');
    const breathingCard = document.getElementById('cardBreathing');
    const breathingEl = document.getElementById('valBreathing');
    const breathingDetailEl = document.getElementById('valBreathingDetail');

    if (stateEl) {
        if (pres.state === 2) {
            stateEl.innerText = 'Motion';
            if (card) { card.classList.add('presence-active'); card.classList.add('motion-active'); }
        } else if (pres.state === 1) {
            stateEl.innerText = 'Presence';
            if (card) { card.classList.add('presence-active'); card.classList.remove('motion-active'); }
        } else if (pres.state === 3) {
            stateEl.innerText = 'Motion & Presence';
            if (card) { card.classList.add('presence-active'); card.classList.add('motion-active'); }
        } else {
            stateEl.innerText = 'No Target';
            if (card) { card.classList.remove('presence-active'); card.classList.remove('motion-active'); }
        }
    }

    if (distEl && pres.dist !== undefined) {
        distEl.innerText = (pres.dist > 0) ? `Distance: ${pres.dist.toFixed(2)} m` : `Distance: -- m`;
    }

    if (breathingEl) {
        const isBreathingActive = (pres.breathing === true || pres.state === 1 || pres.state === 3);
        if (isBreathingActive) {
            breathingEl.innerText = (pres.state === 1) ? 'Resting / Breathing' : 'Breathing Detected';
            breathingEl.classList.add('air-good');
            if (breathingCard) breathingCard.classList.add('breathing-active');
            if (breathingDetailEl) {
                const energyVal = (pres.energy !== undefined && pres.energy > 0) ? `Micro-Motion: ${pres.energy}%` : 'Micro-Motion Active';
                breathingDetailEl.innerText = energyVal;
            }
        } else {
            breathingEl.innerText = 'No Target';
            breathingEl.classList.remove('air-good');
            if (breathingCard) breathingCard.classList.remove('breathing-active');
            if (breathingDetailEl) {
                breathingDetailEl.innerText = 'Micro-Motion: Idle';
            }
        }
    }

    if (pres.status_led !== undefined) {
        ledStates.radar_status = !!pres.status_led;
    }
    if (pres.output_led !== undefined) {
        ledStates.radar_output = !!pres.output_led;
    }
    updateLedControlsUI();
}

function updatePresenceFromTelemetry(msg) {
    if (msg.presence_state !== undefined) {
        updatePresenceUI({
            state: msg.presence_state,
            dist: msg.presence_dist,
            breathing: msg.presence_breathing,
            energy: msg.presence_energy,
            online: msg.presence_online
        });
    }
}

let currentUptimeSecs = 0;

function formatUptime(totalSecs) {
    totalSecs = Math.max(0, Math.floor(totalSecs));
    const d = Math.floor(totalSecs / 86400);
    const h = Math.floor((totalSecs % 86400) / 3600);
    const m = Math.floor((totalSecs % 3600) / 60);
    const s = totalSecs % 60;
    if (d > 0) return `Up: ${d}d ${h}h ${m}m`;
    if (h > 0) return `Up: ${h}h ${m}m ${s}s`;
    if (m > 0) return `Up: ${m}m ${s}s`;
    return `Up: ${s}s`;
}

function updateUptimeUI(seconds) {
    seconds = Number(seconds) || 0;
    // Detect controller reboot if uptime drops significantly
    if (seconds < currentUptimeSecs - 5) {
        currentUptimeSecs = seconds;
        const el = document.getElementById('statUptime');
        if (el) el.innerText = formatUptime(currentUptimeSecs);
        return;
    }

    // Only advance when seconds increment forward (prevents 500ms telemetry jitter)
    if (seconds > currentUptimeSecs || currentUptimeSecs === 0) {
        currentUptimeSecs = seconds;
        const el = document.getElementById('statUptime');
        if (el) el.innerText = formatUptime(currentUptimeSecs);
    }
}

function updateRssiUI(rssi) {
    const el = document.getElementById('statRssi');
    if (el) {
        if (rssi !== undefined && rssi !== null && rssi !== 0 && !isNaN(rssi)) {
            el.innerText = `WiFi: ${rssi} dBm`;
        } else {
            el.innerText = `WiFi: -- dBm`;
        }
    }
}

function updateSystemUI(sys) {
    if (sys.uptime !== undefined) {
        updateUptimeUI(sys.uptime);
    }
    if (sys.rssi !== undefined) {
        updateRssiUI(sys.rssi);
    }
    if (sys.ip !== undefined) {
        const ipEl = document.getElementById('wifiCurIp');
        if (ipEl) ipEl.innerText = sys.ip;
    }
    if (sys.ssid !== undefined) {
        const ssidEl = document.getElementById('wifiCurSsid');
        if (ssidEl) ssidEl.innerText = sys.ssid;
    }
    if (sys.status_led !== undefined) {
        ledStates.controller = !!sys.status_led;
        updateLedControlsUI();
    }
}

// User Actions
function toggleRelay(id) {
    sendWsMessage({
        type: 'relay_toggle',
        id: id
    });
}

function setAllRelays(state) {
    for (let i = 1; i <= RELAY_LABELS.length; i++) {
        sendWsMessage({
            type: 'relay',
            id: i,
            state: state
        });
    }
}

function toggleDimmer(id) {
    if (fanAnimFrames[id]) {
        cancelAnimationFrame(fanAnimFrames[id]);
        fanAnimFrames[id] = null;
    }
    const card = document.getElementById(`dimmerCard${id}`);
    const isCurrentlyActive = card ? card.classList.contains('active') : false;
    const newState = !isCurrentlyActive;
    const currentLevel = dimmerState[id].level > 0 ? dimmerState[id].level : 50;
    const targetLevel = newState ? currentLevel : 0;

    sendWsMessage({
        type: 'dimmer',
        id: id,
        level: targetLevel,
        state: newState
    });

    animateFanToLevel(id, targetLevel);
}

// ==============================================================================
// Settings Modal & Subsystem Handlers (Automation, Wi-Fi & OTA)
// ==============================================================================

let selectedOtaFile = null;

async function fetchWifiStatus() {
    try {
        const res = await fetch('/api/status');
        if (res.ok) {
            const data = await res.json();
            if (data.system) {
                if (data.system.ssid) {
                    const sEl = document.getElementById('wifiCurSsid');
                    if (sEl) sEl.innerText = data.system.ssid;
                    const inEl = document.getElementById('wifiSsidInput');
                    if (inEl && !inEl.value) inEl.value = data.system.ssid;
                }
                if (data.system.ip) {
                    const ipEl = document.getElementById('wifiCurIp');
                    if (ipEl) ipEl.innerText = data.system.ip;
                }
            }
        }
    } catch (e) {
        if (window.location.hostname && window.location.hostname !== 'room-controller.local' && window.location.hostname !== 'localhost') {
            const ipEl = document.getElementById('wifiCurIp');
            if (ipEl) ipEl.innerText = window.location.hostname;
        }
    }
}

// Open Settings Modal
function openSettings() {
    const backdrop = document.getElementById('settingsModalBackdrop');
    if (backdrop) {
        backdrop.style.display = 'flex';
        backdrop.classList.add('open');
        updateLedControlsUI();
        applyChannelNamesToUI();
        updateCardOrderInputs();
        fetchLedSettings();
        fetchPowerConfig();
        fetchAutomationSettings();
        fetchWifiStatus();
        updateOtaPillSelection();
    }
}

// Close Settings Modal
function closeSettings() {
    const backdrop = document.getElementById('settingsModalBackdrop');
    if (backdrop) {
        backdrop.style.display = 'none';
        backdrop.classList.remove('open');
    }
}

function closeSettingsOnBackdrop(e) {
    if (e.target && e.target.id === 'settingsModalBackdrop') {
        closeSettings();
    }
}

// Keyboard ESC to close modal
document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') {
        closeSettings();
    }
});

// Tab Switching
function switchSettingsTab(tabName) {
    const tabs = ['automation', 'leds', 'wifi', 'mqtt', 'ota', 'system'];
    tabs.forEach(t => {
        const btn = document.getElementById(`tabBtn${t.charAt(0).toUpperCase() + t.slice(1)}`);
        const content = document.getElementById(`tabContent${t.charAt(0).toUpperCase() + t.slice(1)}`);
        if (btn && content) {
            if (t === tabName) {
                btn.classList.add('active');
                content.classList.add('active');
            } else {
                btn.classList.remove('active');
                content.classList.remove('active');
            }
        }
    });

    if (tabName === 'automation') {
        fetchAutomationSettings();
    } else if (tabName === 'leds') {
        updateLedControlsUI();
        updatePowerLoggingUI();
        applyChannelNamesToUI();
        updateCardOrderInputs();
    } else if (tabName === 'wifi') {
        fetchWifiStatus();
    } else if (tabName === 'mqtt') {
        fetchMqttConfig();
    } else if (tabName === 'ota') {
        updateOtaPillSelection();
    }
}

// Status Lights Control & State Tracker
let ledStates = {
    radar_status: false,
    radar_output: false,
    controller: true
};

async function fetchLedSettings() {
    try {
        const res = await fetch('/api/mmwave/led');
        if (res.ok) {
            const data = await res.json();
            if (data.status_led !== undefined) ledStates.radar_status = !!data.status_led;
            if (data.output_led !== undefined) ledStates.radar_output = !!data.output_led;
            updateLedControlsUI();
        }
    } catch (e) {
        console.error('Failed to fetch radar LED settings:', e);
    }
    try {
        const resSys = await fetch('/api/system/led');
        if (resSys.ok) {
            const dataSys = await resSys.json();
            if (dataSys.status_led !== undefined) ledStates.controller = !!dataSys.status_led;
            updateLedControlsUI();
        }
    } catch (e) {
        console.error('Failed to fetch system LED settings:', e);
    }
}

function updateLedControlsUI() {
    // Radar Status Light
    const toggleRadarStatus = document.getElementById('toggleRadarStatus');
    const autoToggleRadarStatus = document.getElementById('autoRadarStatusToggle');
    const btnRadarStatus = document.getElementById('btnToggleRadarStatus');
    const btnAutoRadarStatus = document.getElementById('btnAutoRadarStatusToggle');
    
    if (toggleRadarStatus) toggleRadarStatus.checked = !!ledStates.radar_status;
    if (autoToggleRadarStatus) autoToggleRadarStatus.checked = !!ledStates.radar_status;
    
    if (btnRadarStatus) {
        btnRadarStatus.innerText = ledStates.radar_status ? 'Turn OFF' : 'Turn ON';
        if (ledStates.radar_status) btnRadarStatus.classList.add('active-blue');
        else btnRadarStatus.classList.remove('active-blue');
    }
    if (btnAutoRadarStatus) {
        btnAutoRadarStatus.innerText = ledStates.radar_status ? 'Turn OFF' : 'Turn ON';
        if (ledStates.radar_status) btnAutoRadarStatus.classList.add('active-blue');
        else btnAutoRadarStatus.classList.remove('active-blue');
    }

    // Radar Output Light
    const toggleRadarOutput = document.getElementById('toggleRadarOutput');
    const btnRadarOutput = document.getElementById('btnToggleRadarOutput');
    if (toggleRadarOutput) toggleRadarOutput.checked = !!ledStates.radar_output;
    if (btnRadarOutput) {
        btnRadarOutput.innerText = ledStates.radar_output ? 'Turn OFF' : 'Turn ON';
        if (ledStates.radar_output) btnRadarOutput.classList.add('active-blue');
        else btnRadarOutput.classList.remove('active-blue');
    }

    // Controller LED
    const toggleControllerLed = document.getElementById('toggleControllerLed');
    const btnControllerLed = document.getElementById('btnToggleControllerLed');
    if (toggleControllerLed) toggleControllerLed.checked = !!ledStates.controller;
    if (btnControllerLed) {
        btnControllerLed.innerText = ledStates.controller ? 'Turn OFF' : 'Turn ON';
        if (ledStates.controller) btnControllerLed.classList.add('active-blue');
        else btnControllerLed.classList.remove('active-blue');
    }
}

async function setRadarLed(which, enable) {
    if (which === 'status') ledStates.radar_status = enable;
    if (which === 'output') ledStates.radar_output = enable;
    updateLedControlsUI();

    const payload = {};
    if (which === 'status') payload.status_led = enable;
    if (which === 'output') payload.output_led = enable;

    // Send HTTP POST FIRST to guarantee backend execution & persistence to Winbond
    try {
        await fetch('/api/mmwave/led', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
    } catch (e) {
        console.error('REST failed:', e);
    }

    // Also broadcast via WebSocket
    if (ws && ws.readyState === WebSocket.OPEN) {
        sendWsMessage({ type: 'mmwave_led', ...payload });
    }

    showLedFeedback(`Radar ${which === 'status' ? 'Status' : 'Detection'} LED turned ${enable ? 'ON' : 'OFF'}`);
}

function toggleRadarLed(which) {
    if (which === 'status') setRadarLed('status', !ledStates.radar_status);
    if (which === 'output') setRadarLed('output', !ledStates.radar_output);
}

async function setControllerStatusLed(enable) {
    ledStates.controller = enable;
    updateLedControlsUI();

    try {
        await fetch('/api/system/led', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ status_led: enable })
        });
    } catch (e) {
        console.error('REST failed:', e);
    }

    if (ws && ws.readyState === WebSocket.OPEN) {
        sendWsMessage({ type: 'system_led', status_led: enable });
    }

    showLedFeedback(`Controller status light turned ${enable ? 'ON' : 'OFF'}`);
}

function toggleControllerStatusLed() {
    setControllerStatusLed(!ledStates.controller);
}

async function setAllLights(enable) {
    await setRadarLed('status', enable);
    await setRadarLed('output', enable);
    await setControllerStatusLed(enable);
    showLedFeedback(enable ? 'All status lights turned ON' : 'All status lights turned OFF');
}

function showLedFeedback(text) {
    const el = document.getElementById('ledSaveStatus');
    if (el) {
        el.innerText = text;
        el.style.opacity = '1';
        setTimeout(() => { if (el) el.innerText = ''; }, 3000);
    }
}

// ==============================================================================
// Power Consumption Logging & 24-Hour Interactive Chart
// ==============================================================================

let powerLoggingEnabled = false;
let powerHistoryData = [];

async function fetchPowerConfig() {
    try {
        const res = await fetch('/api/power/config');
        if (res.ok) {
            const data = await res.json();
            powerLoggingEnabled = !!data.logging_enabled;
            updatePowerLoggingUI();

            if (data.daily_kwh !== undefined) {
                const el = document.getElementById('valDailyKwh');
                if (el) el.innerText = Number(data.daily_kwh).toFixed(3);
            }
            if (data.monthly_kwh !== undefined) {
                const el = document.getElementById('valMonthlyKwh');
                if (el) el.innerText = Number(data.monthly_kwh).toFixed(3);
            }
            if (data.peak_power !== undefined) {
                const el = document.getElementById('valPeakPower');
                if (el) el.innerText = Number(data.peak_power).toFixed(1);
            }
            if (data.avg_power !== undefined) {
                const el = document.getElementById('valAvgPower');
                if (el) el.innerText = Number(data.avg_power).toFixed(1);
            }
        }
    } catch (e) {
        console.error('Failed to fetch power config:', e);
    }
}

async function fetchPowerHistory() {
    try {
        const res = await fetch('/api/power/history');
        if (res.ok) {
            const data = await res.json();
            powerLoggingEnabled = !!data.logging_enabled;
            updatePowerLoggingUI();

            if (data.daily_kwh !== undefined) {
                const el = document.getElementById('valDailyKwh');
                if (el) el.innerText = Number(data.daily_kwh).toFixed(3);
            }
            if (data.monthly_kwh !== undefined) {
                const el = document.getElementById('valMonthlyKwh');
                if (el) el.innerText = Number(data.monthly_kwh).toFixed(3);
            }
            if (data.peak_power !== undefined) {
                const el = document.getElementById('valPeakPower');
                if (el) el.innerText = Number(data.peak_power).toFixed(1);
            }
            if (data.avg_power !== undefined) {
                const el = document.getElementById('valAvgPower');
                if (el) el.innerText = Number(data.avg_power).toFixed(1);
            }

            powerHistoryData = data.history || [];
            if (powerLoggingEnabled) {
                renderPowerChart(powerHistoryData);
            }
        }
    } catch (e) {
        console.error('Failed to fetch power history:', e);
    }
}

async function setPowerLogging(enable) {
    powerLoggingEnabled = enable;
    updatePowerLoggingUI();

    try {
        await fetch('/api/power/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ logging_enabled: enable })
        });
    } catch (e) {
        console.error('Failed to set power logging:', e);
    }

    if (ws && ws.readyState === WebSocket.OPEN) {
        sendWsMessage({ type: 'power_log_config', logging_enabled: enable });
    }

    if (enable) {
        fetchPowerHistory();
    }
    showLedFeedback(`Power logging turned ${enable ? 'ON' : 'OFF'}`);
}

function togglePowerLogging() {
    setPowerLogging(!powerLoggingEnabled);
}

async function clearPowerLogs() {
    if (!confirm('Are you sure you want to clear all power logs from Winbond flash memory?')) return;
    try {
        await fetch('/api/power/clear', { method: 'POST' });
        powerHistoryData = [];
        renderPowerChart([]);
        const dEl = document.getElementById('valDailyKwh');
        if (dEl) dEl.innerText = '0.000';
        const mEl = document.getElementById('valMonthlyKwh');
        if (mEl) mEl.innerText = '0.000';
        const pEl = document.getElementById('valPeakPower');
        if (pEl) pEl.innerText = '0.0';
        const aEl = document.getElementById('valAvgPower');
        if (aEl) aEl.innerText = '0.0';
        showLedFeedback('Power history erased from Winbond flash');
    } catch (e) {
        console.error('Failed to clear power logs:', e);
    }
}

function updatePowerLoggingUI() {
    const rowEnergy = document.getElementById('rowEnergyConsumption');
    const tabBar = document.getElementById('historyTabBar');
    const drawer = document.getElementById('historyChartDrawer');
    if (rowEnergy) {
        rowEnergy.style.display = powerLoggingEnabled ? 'grid' : 'none';
    }
    if (tabBar) {
        tabBar.style.display = powerLoggingEnabled ? 'flex' : 'none';
    }
    if (!powerLoggingEnabled && drawer) {
        drawer.classList.remove('open');
        const btn = document.getElementById('btnToggleHistoryChart');
        if (btn) btn.classList.remove('active');
    }

    const toggle = document.getElementById('togglePowerLog');
    if (toggle) toggle.checked = powerLoggingEnabled;

    const btn = document.getElementById('btnTogglePowerLog');
    if (btn) {
        btn.innerText = powerLoggingEnabled ? 'Disable Logging' : 'Enable Logging';
        if (powerLoggingEnabled) {
            btn.classList.add('active-blue');
        } else {
            btn.classList.remove('active-blue');
        }
    }
    const chip = document.getElementById('chipPowerLogStatus');
    if (chip) {
        chip.innerText = powerLoggingEnabled ? 'Logging Active' : 'Logging Paused';
        chip.style.opacity = powerLoggingEnabled ? '1' : '0.4';
    }
}

function toggleHistoryChart() {
    const drawer = document.getElementById('historyChartDrawer');
    const btn = document.getElementById('btnToggleHistoryChart');
    if (!drawer || !btn) return;

    const isOpen = drawer.classList.toggle('open');
    btn.classList.toggle('active', isOpen);

    if (isOpen) {
        setTimeout(() => {
            renderPowerChart(powerHistoryData);
        }, 120);
    }
}

function renderPowerChart(points) {
    const canvas = document.getElementById('powerChartCanvas');
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const container = canvas.parentElement;
    const rect = container.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    const width = rect.width || 800;
    const height = rect.height || 260;

    canvas.width = width * dpr;
    canvas.height = height * dpr;
    canvas.style.width = width + 'px';
    canvas.style.height = height + 'px';
    ctx.scale(dpr, dpr);

    ctx.clearRect(0, 0, width, height);

    if (!points || points.length === 0) {
        ctx.fillStyle = 'rgba(148, 163, 184, 0.6)';
        ctx.font = '13px "Plus Jakarta Sans", sans-serif';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText('No power consumption history logged yet. Recording in progress...', width / 2, height / 2);
        return;
    }

    const padding = { top: 25, right: 25, bottom: 35, left: 55 };
    const chartW = width - padding.left - padding.right;
    const chartH = height - padding.top - padding.bottom;

    // Find max Watts
    let maxW = 0;
    for (let p of points) {
        if (p.w > maxW) maxW = p.w;
    }
    maxW = Math.max(50, Math.ceil((maxW * 1.15) / 50) * 50);
    const minW = 0;

    // Horizontal gridlines & Y labels
    const yDivisions = 4;
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.08)';
    ctx.lineWidth = 1;
    ctx.fillStyle = 'rgba(148, 163, 184, 0.7)';
    ctx.font = '11px "Plus Jakarta Sans", sans-serif';
    ctx.textAlign = 'right';
    ctx.textBaseline = 'middle';

    for (let i = 0; i <= yDivisions; i++) {
        const val = Math.round(minW + (maxW - minW) * (i / yDivisions));
        const y = padding.top + chartH - (i / yDivisions) * chartH;

        ctx.beginPath();
        ctx.moveTo(padding.left, y);
        ctx.lineTo(width - padding.right, y);
        ctx.stroke();

        ctx.fillText(val + ' W', padding.left - 10, y);
    }

    const n = points.length;
    const getX = (idx) => padding.left + (idx / (n > 1 ? n - 1 : 1)) * chartW;
    const getY = (w) => padding.top + chartH - ((w - minW) / (maxW - minW)) * chartH;

    // X-Axis labels (-24h, -18h, -12h, -6h, Now)
    const xLabels = ['24h ago', '18h ago', '12h ago', '6h ago', 'Now'];
    ctx.textAlign = 'center';
    ctx.textBaseline = 'top';
    ctx.fillStyle = 'rgba(148, 163, 184, 0.7)';
    for (let i = 0; i < xLabels.length; i++) {
        const x = padding.left + (i / (xLabels.length - 1)) * chartW;
        ctx.fillText(xLabels[i], x, padding.top + chartH + 10);
    }

    // Line Path
    const linePath = new Path2D();
    linePath.moveTo(getX(0), getY(points[0].w));
    for (let i = 1; i < n; i++) {
        const prevX = getX(i - 1);
        const prevY = getY(points[i - 1].w);
        const curX = getX(i);
        const curY = getY(points[i].w);
        const midX = (prevX + curX) / 2;
        linePath.bezierCurveTo(midX, prevY, midX, curY, curX, curY);
    }

    // Area Gradient Fill
    const areaPath = new Path2D(linePath);
    areaPath.lineTo(getX(n - 1), padding.top + chartH);
    areaPath.lineTo(getX(0), padding.top + chartH);
    areaPath.closePath();

    const gradient = ctx.createLinearGradient(0, padding.top, 0, padding.top + chartH);
    gradient.addColorStop(0, 'rgba(6, 182, 212, 0.35)');
    gradient.addColorStop(0.7, 'rgba(6, 182, 212, 0.08)');
    gradient.addColorStop(1, 'rgba(6, 182, 212, 0.00)');
    ctx.fillStyle = gradient;
    ctx.fill(areaPath);

    // Stroke the line
    ctx.strokeStyle = '#06b6d4';
    ctx.lineWidth = 2.5;
    ctx.shadowColor = 'rgba(6, 182, 212, 0.6)';
    ctx.shadowBlur = 8;
    ctx.stroke(linePath);
    ctx.shadowBlur = 0;

    canvas._chartMeta = { points, padding, chartW, chartH, minW, maxW, getX, getY };
}

function initPowerChart() {
    const canvas = document.getElementById('powerChartCanvas');
    const tooltip = document.getElementById('chartTooltip');
    if (!canvas || !tooltip) return;

    function handleHover(e) {
        const meta = canvas._chartMeta;
        if (!meta || !meta.points || meta.points.length === 0) return;

        const rect = canvas.getBoundingClientRect();
        const clientX = e.touches ? e.touches[0].clientX : e.clientX;
        const mouseX = clientX - rect.left;

        if (mouseX < meta.padding.left || mouseX > rect.width - meta.padding.right) {
            tooltip.style.display = 'none';
            renderPowerChart(meta.points);
            return;
        }

        const normX = (mouseX - meta.padding.left) / meta.chartW;
        const idx = Math.min(meta.points.length - 1, Math.max(0, Math.round(normX * (meta.points.length - 1))));
        const pt = meta.points[idx];
        const ptX = meta.getX(idx);
        const ptY = meta.getY(pt.w);

        renderPowerChart(meta.points);
        const ctx = canvas.getContext('2d');
        const dpr = window.devicePixelRatio || 1;
        ctx.save();
        ctx.scale(dpr, dpr);

        // Vertical crosshair
        ctx.beginPath();
        ctx.setLineDash([4, 4]);
        ctx.strokeStyle = 'rgba(6, 182, 212, 0.6)';
        ctx.lineWidth = 1.5;
        ctx.moveTo(ptX, meta.padding.top);
        ctx.lineTo(ptX, meta.padding.top + meta.chartH);
        ctx.stroke();

        // Glowing point
        ctx.beginPath();
        ctx.arc(ptX, ptY, 5, 0, Math.PI * 2);
        ctx.fillStyle = '#ffffff';
        ctx.shadowColor = '#06b6d4';
        ctx.shadowBlur = 10;
        ctx.fill();
        ctx.lineWidth = 2;
        ctx.strokeStyle = '#06b6d4';
        ctx.stroke();
        ctx.restore();

        // Position Tooltip
        let timeStr = 'Sample #' + (idx + 1);
        if (pt.t > 1700000000) {
            const d = new Date(pt.t * 1000);
            timeStr = d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
        } else {
            const minsAgo = Math.round(((meta.points.length - 1 - idx) * 5));
            timeStr = minsAgo === 0 ? 'Now' : `${minsAgo}m ago`;
        }

        tooltip.innerHTML = `
            <div class="tt-time">${timeStr}</div>
            <div class="tt-watts">${pt.w.toFixed(1)} W</div>
            <div style="font-size:11px;color:rgba(148,163,184,0.8);">${pt.v ? pt.v.toFixed(1) + ' V' : ''}</div>
        `;
        tooltip.style.display = 'block';

        let tipLeft = ptX + 15;
        if (tipLeft + 120 > rect.width) {
            tipLeft = ptX - 130;
        }
        tooltip.style.left = `${Math.max(10, tipLeft)}px`;
        tooltip.style.top = `${Math.max(10, ptY - 30)}px`;
    }

    canvas.addEventListener('mousemove', handleHover);
    canvas.addEventListener('mouseleave', () => {
        tooltip.style.display = 'none';
        if (canvas._chartMeta && canvas._chartMeta.points) {
            renderPowerChart(canvas._chartMeta.points);
        }
    });
    canvas.addEventListener('touchmove', handleHover, { passive: true });
    canvas.addEventListener('touchend', () => {
        tooltip.style.display = 'none';
        if (canvas._chartMeta && canvas._chartMeta.points) {
            renderPowerChart(canvas._chartMeta.points);
        }
    });
    window.addEventListener('resize', () => {
        if (canvas._chartMeta && canvas._chartMeta.points) {
            renderPowerChart(canvas._chartMeta.points);
        }
    });
}

function getAutoItemIcon(type) {
    if (type === 'fan') {
        return `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <circle cx="12" cy="12" r="2.5" fill="currentColor"/>
            <path d="M12 12c-2-2.5-4-3-6-2.5s-3 3-1.5 5 4.5 2 6.5-.5z" fill="currentColor" fill-opacity="0.75"/>
            <path d="M12 12c2.5-2 3-4 2.5-6s-3-3-5-1.5-2 4.5.5 6.5z" fill="currentColor" fill-opacity="0.75"/>
            <path d="M12 12c2 2.5 4 3 6 2.5s3-3 1.5-5-4.5-2-6.5.5z" fill="currentColor" fill-opacity="0.75"/>
            <path d="M12 12c-2.5 2-3 4-2.5 6s3 3 5 1.5 2-4.5-.5-6.5z" fill="currentColor" fill-opacity="0.75"/>
        </svg>`;
    } else if (type === 'bulb') {
        return `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <path d="M9 18h6"/>
            <path d="M10 22h4"/>
            <path d="M12 2a7 7 0 0 0-7 7c0 2.5 1.5 4.5 3 5.5v1.5h8V14.5c1.5-1 3-3 3-5.5a7 7 0 0 0-7-7z"/>
        </svg>`;
    } else if (type === 'spot') {
        return `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <path d="m14 4 6 6-3 3-6-6 3-3z"/>
            <path d="m11 7-7 7 3 3 7-7"/>
            <line x1="3" y1="18" x2="6" y2="21"/>
        </svg>`;
    } else if (type === 'socket') {
        return `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <rect x="5" y="3" width="14" height="18" rx="3"/>
            <circle cx="10" cy="10" r="1" fill="currentColor"/>
            <circle cx="14" cy="10" r="1" fill="currentColor"/>
            <path d="M10 15h4"/>
        </svg>`;
    } else if (type === 'strip') {
        return `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <rect x="3" y="6" width="18" height="12" rx="2"/>
            <line x1="7" y1="9" x2="7" y2="15"/>
            <line x1="12" y1="9" x2="12" y2="15"/>
            <line x1="17" y1="9" x2="17" y2="15"/>
        </svg>`;
    }
    return `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="6"/></svg>`;
}

// ==========================================================================
// Embedded Automation Engine Controller (Tab 1 & Live Dashboard Badges)
// ==========================================================================

let automationConfig = {
    master: true,
    off_delay: 120,
    fan1: {
        enabled: false,
        target_temp: 26.0,
        temp_low: 24.0,
        temp_med: 27.0,
        temp_high: 30.0,
        speed_low: 40,
        speed_med: 70,
        speed_high: 100
    },
    fan2: {
        mode: 0,
        co2_low: 800,
        co2_med: 1200,
        co2_high: 1800,
        co2_speed_low: 40,
        co2_speed_med: 70,
        co2_speed_high: 100,
        temp_low: 26.0,
        temp_high: 30.0,
        temp_speed_med: 50,
        temp_speed_high: 100
    },
    scenarios: {
        night_quiet: true,
        night_start: 22,
        night_end: 7,
        night_max_speed: 50,
        air_boost: true,
        air_boost_trigger: 1500,
        air_boost_clear: 900,
        air_boost_exhaust: 100,
        air_boost_ceiling: 40
    },
    relays: [
        { id: 1, auto: false },
        { id: 2, auto: false },
        { id: 3, auto: false },
        { id: 4, auto: false },
        { id: 5, auto: false },
        { id: 6, auto: false }
    ]
};

function getRelayIconSvg(type) {
    if (type === 'lamp') {
        return `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <path d="M12 2v6m-6 6h12l-2-6H8l-2 6zM9 17h6M10 20h4"/>
        </svg>`;
    } else if (type === 'spot') {
        return `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <rect x="7" y="2" width="10" height="4" rx="1"/>
            <path d="M7 6 5 13h14l-2-7"/>
            <circle cx="12" cy="18" r="3"/>
            <line x1="12" y1="13" x2="12" y2="15"/>
        </svg>`;
    } else if (type === 'socket') {
        return `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <rect x="4" y="2" width="16" height="20" rx="4"/>
            <circle cx="9" cy="11" r="1.5" fill="currentColor"/>
            <circle cx="15" cy="11" r="1.5" fill="currentColor"/>
        </svg>`;
    } else if (type === 'strip') {
        return `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <rect x="3" y="8" width="18" height="8" rx="2"/>
            <circle cx="7.5" cy="12" r="1" fill="currentColor"/>
            <circle cx="12" cy="12" r="1" fill="currentColor"/>
            <circle cx="16.5" cy="12" r="1" fill="currentColor"/>
        </svg>`;
    }
    return '';
}

function getStatusIconSvg(status, relayId) {
    status = String(status || '');
    if (status.includes('Night Blocked')) {
        return `<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"/></svg>`;
    } else if (status.includes('Time Restricted') || status.includes('Scheduled')) {
        return `<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/></svg>`;
    } else if (status.includes('Manual')) {
        return `<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M18 11V6a2 2 0 0 0-4 0v4M14 10V4a2 2 0 0 0-4 0v7M10 10.5V6a2 2 0 0 0-4 0v8a7 7 0 0 0 14 0v-4a2 2 0 0 0-4 0v1"/></svg>`;
    } else if (status.includes('Presence') || status.includes('Active')) {
        return `<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"/><circle cx="12" cy="7" r="4"/></svg>`;
    }
    return `<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 2v2M12 20v2M4.93 4.93l1.41 1.41M17.66 17.66l1.41 1.41M2 12h2M20 12h2M6.34 17.66l-1.41 1.41M19.07 4.93l-1.41 1.41"/></svg>`;
}

// Initialize 6-Channel Lighting Automation Cards Grid (Matching Image 1)
function initAutomationSwitchesGrid() {
    const grid = document.getElementById('autoSwitchesGrid');
    if (!grid) return;
    grid.innerHTML = '';

    const relayInfo = [
        { id: 1, defaultName: 'Main Light 1', pin: 'GPB2 • Pin 16', theme: 'cyan', icon: 'lamp' },
        { id: 2, defaultName: 'Main Light 2', pin: 'GPB3 • Pin 15', theme: 'cyan', icon: 'lamp' },
        { id: 3, defaultName: 'Spotlights', pin: 'GPB4 • Pin 14', theme: 'purple', icon: 'spot' },
        { id: 4, defaultName: 'Wall Socket 1', pin: 'GPB5 • Pin 13', theme: 'purple', icon: 'socket' },
        { id: 5, defaultName: 'Wall Socket 2', pin: 'GPB6 • Pin 12', theme: 'purple', icon: 'socket' },
        { id: 6, defaultName: 'Ambient Strip', pin: 'GPB7 • Pin 11', theme: 'amber', icon: 'strip' }
    ];

    relayInfo.forEach(info => {
        const i = info.id;
        const displayName = (channelNames && channelNames[`relay${i}`]) ? channelNames[`relay${i}`] : info.defaultName;
        const rData = (automationConfig.relays && automationConfig.relays[i - 1]) ? automationConfig.relays[i - 1] : { auto: false, mode: 1, night_allowed: false, timeout: 0 };
        const isAuto = !!rData.auto;

        const card = document.createElement('div');
        card.className = `light-auto-card theme-${info.theme}`;
        card.id = `lightAutoCard_${i}`;
        card.innerHTML = `
            <div class="light-auto-header">
                <div class="light-header-left">
                    <div class="light-hex-badge light-hex-${info.theme}">
                        <svg class="hex-frame" width="38" height="38" viewBox="0 0 64 64">
                            <polygon points="32,4 58,19 58,45 32,60 6,45 6,19" fill="none" stroke="currentColor" stroke-width="1.8" stroke-opacity="0.4"/>
                            <polygon points="32,8 54,21 54,43 32,56 10,43 10,21" fill="rgba(255,255,255,0.02)" stroke="currentColor" stroke-width="2"/>
                        </svg>
                        <div class="light-hex-icon">
                            ${getRelayIconSvg(info.icon)}
                        </div>
                    </div>
                    <div class="light-auto-title-wrap">
                        <span class="light-auto-title" id="lightCardName_${i}">${displayName}</span>
                        <span class="light-auto-pin">Relay ${i} • ${info.pin}</span>
                    </div>
                </div>
                <div class="light-auto-toggle-wrap">
                    <span class="light-auto-mode-label ${isAuto ? '' : 'manual'}" id="lightAutoModeLabel_${i}">${isAuto ? 'AUTO' : 'MANUAL'}</span>
                    <label class="cyber-toggle-green">
                        <input type="checkbox" id="lightAutoToggle_${i}" ${isAuto ? 'checked' : ''} onchange="toggleLightAuto(${i})">
                        <span class="cyber-slider-green"></span>
                    </label>
                </div>
            </div>

            <div class="light-trigger-section">
                <span class="light-trigger-label">Trigger Mode</span>
                <div class="light-mode-pills mode-theme-${info.theme}">
                    <button class="light-mode-pill ${rData.mode === 1 ? 'active' : ''}" id="btnLightMode_${i}_1" onclick="setLightMode(${i}, 1)">Presence</button>
                    <button class="light-mode-pill ${rData.mode === 2 ? 'active' : ''}" id="btnLightMode_${i}_2" onclick="setLightMode(${i}, 2)">Presence + Time</button>
                    <button class="light-mode-pill ${rData.mode === 3 ? 'active' : ''}" id="btnLightMode_${i}_3" onclick="setLightMode(${i}, 3)">Scheduled</button>
                </div>
            </div>

            <div class="light-controls-row">
                <div class="light-delay-col">
                    <span class="light-ctrl-label">Off Delay</span>
                    <select class="cyber-select light-select" id="lightTimeout_${i}" onchange="updateLightTimeout(${i})">
                        <option value="0" ${rData.timeout === 0 ? 'selected' : ''}>Global (120s)</option>
                        <option value="60" ${rData.timeout === 60 ? 'selected' : ''}>1 Min</option>
                        <option value="300" ${rData.timeout === 300 ? 'selected' : ''}>5 Min</option>
                        <option value="600" ${rData.timeout === 600 ? 'selected' : ''}>10 Min</option>
                        <option value="1800" ${rData.timeout === 1800 ? 'selected' : ''}>30 Min</option>
                    </select>
                </div>
                <div class="light-night-col">
                    <span class="light-ctrl-label">Night Allowed</span>
                    <label class="custom-cyber-checkbox">
                        <input type="checkbox" id="lightNightAllowed_${i}" ${rData.night_allowed ? 'checked' : ''} onchange="toggleLightNightAllowed(${i})">
                        <span class="checkbox-mark ${info.theme === 'amber' ? 'amber-mark' : ''}"></span>
                    </label>
                </div>
            </div>

            <div class="light-live-status-row status-theme-${info.theme}">
                <span class="light-status-icon" id="lightStatusIcon_${i}">
                    ${getStatusIconSvg(rData.status, i)}
                </span>
                <span class="light-live-status-text" id="lightStatusText_${i}">${rData.status || (isAuto ? 'Active' : 'Manual Mode')}</span>
            </div>
        `;
        grid.appendChild(card);
    });
}

// Preset Selection (0: Normal, 1: Movie, 2: Cleaning, 3: Away)
async function selectPreset(preset) {
    preset = parseInt(preset, 10);
    [0, 1, 2, 3].forEach(p => {
        const btn = document.getElementById(`btnPreset_${p}`);
        if (btn) {
            if (p === preset) btn.classList.add('active');
            else btn.classList.remove('active');
        }
    });

    const statusEl = document.getElementById('autoSaveStatus');
    if (statusEl) {
        statusEl.className = 'save-status';
        statusEl.innerText = 'Applying Preset...';
    }

    try {
        const res = await fetch('/api/automation/preset', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ preset: preset })
        });
        if (res.ok) {
            if (statusEl) {
                const names = ['Normal / Auto', 'Movie Mode', 'Cleaning Mode', 'Away Mode'];
                statusEl.innerText = `${names[preset]} Activated!`;
                setTimeout(() => { if (statusEl) statusEl.innerText = ''; }, 2500);
            }
        }
    } catch (e) {
        sendWsMessage({ type: 'apply_preset', preset: preset });
    }
}

// Per-Channel Light Mode Selector
function setLightMode(relayId, mode) {
    mode = parseInt(mode, 10);
    if (!automationConfig.relays) automationConfig.relays = [];
    while (automationConfig.relays.length < relayId) {
        automationConfig.relays.push({ id: automationConfig.relays.length + 1, auto: false, mode: 1 });
    }
    automationConfig.relays[relayId - 1].mode = mode;

    [1, 2, 3].forEach(m => {
        const btn = document.getElementById(`btnLightMode_${relayId}_${m}`);
        if (btn) {
            if (m === mode) btn.classList.add('active');
            else btn.classList.remove('active');
        }
    });
    saveAutomationForm();
}

// Toggle Auto vs Manual on a Light Channel
function toggleLightAuto(relayId) {
    const t = document.getElementById(`lightAutoToggle_${relayId}`);
    const isAuto = t ? t.checked : false;

    if (!automationConfig.relays) automationConfig.relays = [];
    while (automationConfig.relays.length < relayId) {
        automationConfig.relays.push({ id: automationConfig.relays.length + 1, auto: false, mode: 1 });
    }
    automationConfig.relays[relayId - 1].auto = isAuto;

    const label = document.getElementById(`lightAutoModeLabel_${relayId}`);
    if (label) {
        label.innerText = isAuto ? 'AUTO' : 'MANUAL';
        if (isAuto) label.classList.remove('manual');
        else label.classList.add('manual');
    }

    const body = document.getElementById(`lightAutoBody_${relayId}`);
    if (body) {
        if (isAuto) body.classList.remove('disabled');
        else body.classList.add('disabled');
    }

    const dot = document.getElementById(`lightStatusDot_${relayId}`);
    if (dot) {
        if (isAuto) dot.classList.remove('inactive');
        else dot.classList.add('inactive');
    }

    // Reflect on dashboard relay card
    const tag = document.getElementById(`relayAutoTag${relayId}`);
    if (tag) tag.style.display = (automationConfig.master && isAuto) ? 'inline-flex' : 'none';

    saveAutomationForm();
}

// Toggle Night Allowed for a Light Channel
function toggleLightNightAllowed(relayId) {
    const cb = document.getElementById(`lightNightAllowed_${relayId}`);
    if (!automationConfig.relays) automationConfig.relays = [];
    while (automationConfig.relays.length < relayId) {
        automationConfig.relays.push({ id: automationConfig.relays.length + 1, auto: false, mode: 1 });
    }
    automationConfig.relays[relayId - 1].night_allowed = cb ? cb.checked : false;
    saveAutomationForm();
}

// Update Inactivity Timeout for a Light Channel
function updateLightTimeout(relayId) {
    const sel = document.getElementById(`lightTimeout_${relayId}`);
    if (!automationConfig.relays) automationConfig.relays = [];
    while (automationConfig.relays.length < relayId) {
        automationConfig.relays.push({ id: automationConfig.relays.length + 1, auto: false, mode: 1 });
    }
    automationConfig.relays[relayId - 1].timeout = sel ? parseInt(sel.value, 10) : 0;
    saveAutomationForm();
}

// Stepper for Target Temperature (Ceiling Fan)
function stepTargetTemp(delta) {
    let current = (automationConfig.fan1 && automationConfig.fan1.target_temp) ? automationConfig.fan1.target_temp : 26.0;
    current = Math.round((current + delta) * 10) / 10;
    if (current < 18.0) current = 18.0;
    if (current > 35.0) current = 35.0;
    if (!automationConfig.fan1) automationConfig.fan1 = {};
    automationConfig.fan1.target_temp = current;
    const el = document.getElementById('dispTargetTemp');
    if (el) el.innerText = `${current.toFixed(1)}°C`;
    saveAutomationForm();
}

// Exhaust Fan Mode Selector (0: Manual, 1: CO2, 2: Temp, 3: Combined)
function setExhaustMode(mode) {
    mode = parseInt(mode, 10);
    if (!automationConfig.fan2) automationConfig.fan2 = {};
    automationConfig.fan2.mode = mode;
    [0, 1, 2, 3].forEach(m => {
        const btn = document.getElementById(`btnMode_${m}`);
        if (btn) {
            if (m === mode) btn.classList.add('active');
            else btn.classList.remove('active');
        }
    });
    saveAutomationForm();
}

// Update UI with configuration data from backend
function updateAutomationUI(data) {
    if (!data) return;
    automationConfig = data;

    // Master Switch
    const masterEl = document.getElementById('autoMasterToggle');
    if (masterEl && data.master !== undefined) masterEl.checked = !!data.master;

    // Inactivity Delay
    const delayEl = document.getElementById('autoOffDelaySelect');
    if (delayEl && data.off_delay !== undefined) delayEl.value = data.off_delay;

    // Presets
    if (data.preset !== undefined) {
        [0, 1, 2, 3].forEach(p => {
            const btn = document.getElementById(`btnPreset_${p}`);
            if (btn) {
                if (p === data.preset) btn.classList.add('active');
                else btn.classList.remove('active');
            }
        });
    }

    // Ceiling Fan (Fan 1)
    if (data.fan1) {
        const f1Toggle = document.getElementById('autoFan1Toggle');
        if (f1Toggle && data.fan1.enabled !== undefined) f1Toggle.checked = !!data.fan1.enabled;
        const dispTemp = document.getElementById('dispTargetTemp');
        if (dispTemp && data.fan1.target_temp !== undefined) {
            dispTemp.innerText = `${Number(data.fan1.target_temp).toFixed(1)}°C`;
        }
        const f1Head = document.getElementById('autoFan1Heading');
        if (f1Head && channelNames && channelNames.fan1) {
            f1Head.innerText = `${channelNames.fan1} Automation`;
        }
    }

    // Exhaust Fan (Fan 2)
    if (data.fan2 && data.fan2.mode !== undefined) {
        const mode = parseInt(data.fan2.mode, 10);
        [0, 1, 2, 3].forEach(m => {
            const btn = document.getElementById(`btnMode_${m}`);
            if (btn) {
                if (m === mode) btn.classList.add('active');
                else btn.classList.remove('active');
            }
        });
        const f2Head = document.getElementById('autoFan2Heading');
        if (f2Head && channelNames && channelNames.fan2) {
            f2Head.innerText = `${channelNames.fan2} Automation`;
        }
    }

    // Smart Scenarios
    if (data.scenarios) {
        const nqToggle = document.getElementById('autoNightQuietToggle');
        if (nqToggle && data.scenarios.night_quiet !== undefined) {
            nqToggle.checked = !!data.scenarios.night_quiet;
        }
        const abToggle = document.getElementById('autoAirBoostToggle');
        if (abToggle && data.scenarios.air_boost !== undefined) {
            abToggle.checked = !!data.scenarios.air_boost;
        }
    }

    // 6-Channel Lighting Cards
    if (data.relays && Array.isArray(data.relays)) {
        data.relays.forEach(r => {
            const i = r.id;
            const t = document.getElementById(`lightAutoToggle_${i}`);
            if (t) t.checked = !!r.auto;

            const label = document.getElementById(`lightAutoModeLabel_${i}`);
            if (label) {
                label.innerText = r.auto ? 'AUTO' : 'MANUAL';
                if (r.auto) label.classList.remove('manual');
                else label.classList.add('manual');
            }

            const body = document.getElementById(`lightAutoBody_${i}`);
            if (body) {
                if (r.auto) body.classList.remove('disabled');
                else body.classList.add('disabled');
            }

            const dot = document.getElementById(`lightStatusDot_${i}`);
            if (dot) {
                if (r.auto) dot.classList.remove('inactive');
                else dot.classList.add('inactive');
            }

            if (r.mode !== undefined) {
                [1, 2, 3].forEach(m => {
                    const btn = document.getElementById(`btnLightMode_${i}_${m}`);
                    if (btn) {
                        if (m === r.mode) btn.classList.add('active');
                        else btn.classList.remove('active');
                    }
                });
            }

            const cb = document.getElementById(`lightNightAllowed_${i}`);
            if (cb && r.night_allowed !== undefined) cb.checked = !!r.night_allowed;

            const sel = document.getElementById(`lightTimeout_${i}`);
            if (sel && r.timeout !== undefined) sel.value = r.timeout;

            const st = document.getElementById(`lightStatusText_${i}`);
            if (st && r.status) st.innerText = r.status;

            // Dashboard card badge
            const tag = document.getElementById(`relayAutoTag${i}`);
            if (tag) tag.style.display = (data.master && r.auto) ? 'inline-flex' : 'none';
        });
    }

    // Dashboard Fan Badges
    const badgeF1 = document.getElementById('autoBadgeFan1');
    if (badgeF1) badgeF1.style.display = (data.master && data.fan1 && data.fan1.enabled) ? 'inline-flex' : 'none';

    const badgeF2 = document.getElementById('autoBadgeFan2');
    if (badgeF2) badgeF2.style.display = (data.master && data.fan2 && data.fan2.mode !== 0) ? 'inline-flex' : 'none';
}

// Live telemetry updates for automation indicators
function updateLiveAutomationTelemetry(msg) {
    // Badges on Main Dashboard
    const badgeF1 = document.getElementById('autoBadgeFan1');
    if (badgeF1 && msg.fan1_auto !== undefined) {
        badgeF1.style.display = msg.fan1_auto ? 'inline-flex' : 'none';
    }
    const badgeF2 = document.getElementById('autoBadgeFan2');
    if (badgeF2 && msg.fan2_auto !== undefined) {
        badgeF2.style.display = msg.fan2_auto ? 'inline-flex' : 'none';
    }

    // Preset active reflection
    if (msg.auto_preset !== undefined) {
        [0, 1, 2, 3].forEach(p => {
            const btn = document.getElementById(`btnPreset_${p}`);
            if (btn) {
                if (p === msg.auto_preset) btn.classList.add('active');
                else btn.classList.remove('active');
            }
        });
    }

    // Tab 1 Relay Live Status Text & Badges
    for (let i = 1; i <= 6; i++) {
        const autoKey = `relay${i}_auto`;
        const stKey = `relay${i}_auto_st`;
        if (msg[autoKey] !== undefined) {
            const tag = document.getElementById(`relayAutoTag${i}`);
            if (tag) tag.style.display = msg[autoKey] ? 'inline-flex' : 'none';
            const toggle = document.getElementById(`lightAutoToggle_${i}`);
            if (toggle && toggle.checked !== !!msg[autoKey]) toggle.checked = !!msg[autoKey];
            const modeLbl = document.getElementById(`lightAutoModeLabel_${i}`);
            if (modeLbl) {
                modeLbl.innerText = msg[autoKey] ? 'AUTO' : 'MANUAL';
                if (msg[autoKey]) modeLbl.classList.remove('manual');
                else modeLbl.classList.add('manual');
            }
        }
        if (msg[stKey] !== undefined) {
            const el = document.getElementById(`lightStatusText_${i}`);
            if (el) el.innerText = msg[stKey];
            const iconEl = document.getElementById(`lightStatusIcon_${i}`);
            if (iconEl) iconEl.innerHTML = getStatusIconSvg(msg[stKey], i);
        }
    }

    // Tab 1 Fan 1 Live Status Text
    const f1Text = document.getElementById('autoFan1LiveText');
    if (f1Text && msg.fan1_status !== undefined) {
        f1Text.innerText = `Status: ${msg.fan1_status}`;
    }

    // Tab 1 Fan 2 Live Status Text
    const f2Text = document.getElementById('autoFan2LiveText');
    if (f2Text && msg.fan2_status !== undefined) {
        f2Text.innerText = `Status: ${msg.fan2_status}`;
    }

    // Tab 1 Presence & Inactivity Indicator
    const presText = document.getElementById('livePresenceText');
    const presDot = document.querySelector('#livePresenceIndicator .status-dot');
    const countTag = document.getElementById('liveCountdownTag');
    if (presText) {
        if (msg.presence_state > 0) {
            presText.innerText = 'Presence: Occupied (Target Active)';
            if (presDot) presDot.className = 'status-dot';
            if (countTag) countTag.innerText = 'Inactivity: Reset';
        } else {
            presText.innerText = 'Presence: Unoccupied';
            if (presDot) presDot.className = 'status-dot inactive';
            if (countTag && msg.inactivity_rem !== undefined) {
                countTag.innerText = `Auto-OFF in: ${msg.inactivity_rem}s`;
            }
        }
    }

    // Scenario Tags
    const nqTag = document.getElementById('liveNightQuietTag');
    if (nqTag && msg.night_quiet_active !== undefined) {
        if (msg.night_quiet_active) {
            nqTag.innerText = 'Active (Capped 50%)';
            nqTag.className = 'scenario-active-tag active';
        } else {
            nqTag.innerText = 'Inactive (Daytime)';
            nqTag.className = 'scenario-active-tag';
        }
    }

    const abTag = document.getElementById('liveAirBoostTag');
    if (abTag && msg.air_boost_active !== undefined) {
        if (msg.air_boost_active) {
            abTag.innerText = 'BOOST PURGE ACTIVE';
            abTag.className = 'scenario-active-tag boost';
        } else {
            abTag.innerText = 'Normal';
            abTag.className = 'scenario-active-tag';
        }
    }

    // Safety Telemetry Updates
    if (msg.filtered_temp !== undefined) {
        const el = document.getElementById('dispFilteredTemp');
        if (el) el.innerText = Number(msg.filtered_temp).toFixed(1) + '°C';
    }
    if (msg.filtered_co2 !== undefined) {
        const el = document.getElementById('dispFilteredCo2');
        if (el) el.innerText = msg.filtered_co2 + ' ppm';
    }
    if (msg.pwr_stabilized !== undefined) {
        const el = document.getElementById('dispPwrStatus');
        const tag = document.getElementById('livePwrStabilizedTag');
        if (msg.pwr_stabilized) {
            if (el) { el.innerText = 'Stabilized'; el.className = 'safety-stat-val text-cyan'; }
            if (tag) { tag.innerText = 'Active & Guarded'; tag.className = 'chip-presets'; }
        } else {
            const rem = msg.pwr_rem || 0;
            if (el) { el.innerText = `Stabilizing (${rem}s)`; el.className = 'safety-stat-val'; }
            if (tag) { tag.innerText = `Stabilizing (${rem}s)`; tag.className = 'chip-presets chip-active'; }
        }
    }
}

// ==============================================================================
// Automation Safety & Protection Layer
// ==============================================================================
let safetyConfig = {
    t_hyst: 1.5,
    c_hyst: 300,
    man_dur: 1800,
    pwr_delay: 30
};

async function fetchSafetySettings() {
    try {
        const res = await fetch('/api/automation/safety');
        if (res.ok) {
            const data = await res.json();
            updateSafetyUI(data);
        }
    } catch (e) {
        sendWsMessage({ type: 'safety_get' });
    }
}

function updateSafetyUI(data) {
    if (!data) return;
    if (data.t_hyst !== undefined) {
        safetyConfig.t_hyst = parseFloat(data.t_hyst);
        const el = document.getElementById('safetyTempHystVal');
        const badge = document.getElementById('dispTempHyst');
        if (el) el.innerText = safetyConfig.t_hyst.toFixed(1) + '°C';
        if (badge) badge.innerText = safetyConfig.t_hyst.toFixed(1) + '°C';
    }
    if (data.c_hyst !== undefined) {
        safetyConfig.c_hyst = parseInt(data.c_hyst, 10);
        const el = document.getElementById('safetyCo2HystVal');
        const badge = document.getElementById('dispCo2Hyst');
        if (el) el.innerText = safetyConfig.c_hyst + ' ppm';
        if (badge) badge.innerText = safetyConfig.c_hyst + ' ppm';
    }
    if (data.man_dur !== undefined) {
        safetyConfig.man_dur = parseInt(data.man_dur, 10);
        const sel = document.getElementById('safetyManualDurationSelect');
        if (sel) sel.value = String(safetyConfig.man_dur);
    }
    if (data.filtered_temp !== undefined) {
        const el = document.getElementById('dispFilteredTemp');
        if (el) el.innerText = Number(data.filtered_temp).toFixed(1) + '°C';
    }
    if (data.filtered_co2 !== undefined) {
        const el = document.getElementById('dispFilteredCo2');
        if (el) el.innerText = data.filtered_co2 + ' ppm';
    }
    if (data.pwr_stabilized !== undefined) {
        const el = document.getElementById('dispPwrStatus');
        const tag = document.getElementById('livePwrStabilizedTag');
        if (data.pwr_stabilized) {
            if (el) { el.innerText = 'Stabilized'; el.className = 'safety-stat-val text-cyan'; }
            if (tag) { tag.innerText = 'Active & Guarded'; tag.className = 'chip-presets'; }
        } else {
            const rem = data.pwr_rem || 0;
            if (el) { el.innerText = `Stabilizing (${rem}s)`; el.className = 'safety-stat-val'; }
            if (tag) { tag.innerText = `Stabilizing (${rem}s)`; tag.className = 'chip-presets chip-active'; }
        }
    }
}

function stepSafetyTempHyst(delta) {
    let val = Math.round((safetyConfig.t_hyst + delta) * 10) / 10;
    if (val < 0.5) val = 0.5;
    if (val > 5.0) val = 5.0;
    safetyConfig.t_hyst = val;
    const el = document.getElementById('safetyTempHystVal');
    const badge = document.getElementById('dispTempHyst');
    if (el) el.innerText = val.toFixed(1) + '°C';
    if (badge) badge.innerText = val.toFixed(1) + '°C';
    saveSafetyConfig();
}

function stepSafetyCo2Hyst(delta) {
    let val = safetyConfig.c_hyst + delta;
    if (val < 50) val = 50;
    if (val > 500) val = 500;
    safetyConfig.c_hyst = val;
    const el = document.getElementById('safetyCo2HystVal');
    const badge = document.getElementById('dispCo2Hyst');
    if (el) el.innerText = val + ' ppm';
    if (badge) badge.innerText = val + ' ppm';
    saveSafetyConfig();
}

async function saveSafetyConfig() {
    const sel = document.getElementById('safetyManualDurationSelect');
    if (sel) safetyConfig.man_dur = parseInt(sel.value, 10);

    const payload = {
        t_hyst: safetyConfig.t_hyst,
        c_hyst: safetyConfig.c_hyst,
        man_dur: safetyConfig.man_dur
    };

    try {
        await fetch('/api/automation/safety', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
    } catch (e) {
        sendWsMessage({ type: 'safety_set', ...payload });
    }
}

async function clearAllOverrides() {
    try {
        await fetch('/api/automation/safety/clear_override', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({})
        });
    } catch (e) {
        sendWsMessage({ type: 'clear_override' });
    }
}

// Fetch Automation Config from backend
async function fetchAutomationSettings() {
    fetchSafetySettings();
    try {
        const res = await fetch('/api/automation');
        if (res.ok) {
            const data = await res.json();
            updateAutomationUI(data);
        }
    } catch (e) {
        sendWsMessage({ type: 'automation_get' });
    }
}

// Save Automation Form to backend
let autoSaveTimer = null;
async function saveAutomationForm() {
    const statusEl = document.getElementById('autoSaveStatus');
    if (statusEl) {
        statusEl.className = 'save-status';
        statusEl.innerText = 'Saving...';
    }

    const mToggle = document.getElementById('autoMasterToggle');
    const dSelect = document.getElementById('autoOffDelaySelect');
    const f1Toggle = document.getElementById('autoFan1Toggle');
    const nqToggle = document.getElementById('autoNightQuietToggle');
    const abToggle = document.getElementById('autoAirBoostToggle');

    const payload = {
        master: mToggle ? mToggle.checked : true,
        off_delay: dSelect ? parseInt(dSelect.value, 10) : 120,
        fan1: {
            enabled: f1Toggle ? f1Toggle.checked : false,
            target_temp: (automationConfig.fan1 && automationConfig.fan1.target_temp) ? automationConfig.fan1.target_temp : 26.0
        },
        fan2: {
            mode: (automationConfig.fan2 && automationConfig.fan2.mode !== undefined) ? automationConfig.fan2.mode : 0
        },
        scenarios: {
            night_quiet: nqToggle ? nqToggle.checked : true,
            air_boost: abToggle ? abToggle.checked : true
        },
        relays: []
    };

    for (let i = 1; i <= 6; i++) {
        const t = document.getElementById(`lightAutoToggle_${i}`);
        const cb = document.getElementById(`lightNightAllowed_${i}`);
        const sel = document.getElementById(`lightTimeout_${i}`);
        const existing = (automationConfig.relays && automationConfig.relays[i - 1]) ? automationConfig.relays[i - 1] : {};

        payload.relays.push({
            id: i,
            auto: t ? t.checked : !!existing.auto,
            mode: existing.mode !== undefined ? existing.mode : 1,
            night_allowed: cb ? cb.checked : !!existing.night_allowed,
            timeout: sel ? parseInt(sel.value, 10) : (existing.timeout || 0),
            start_h: existing.start_h !== undefined ? existing.start_h : 7,
            start_m: existing.start_m !== undefined ? existing.start_m : 0,
            end_h: existing.end_h !== undefined ? existing.end_h : 22,
            end_m: existing.end_m !== undefined ? existing.end_m : 0,
            quiet_lvl: existing.quiet_lvl !== undefined ? existing.quiet_lvl : 30
        });
    }

    try {
        const res = await fetch('/api/automation', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        if (res.ok) {
            if (statusEl) {
                statusEl.className = 'save-status';
                statusEl.innerText = 'Automation rules saved!';
                if (autoSaveTimer) clearTimeout(autoSaveTimer);
                autoSaveTimer = setTimeout(() => { if (statusEl) statusEl.innerText = ''; }, 2500);
            }
            updateAutomationUI(payload);
        }
    } catch (e) {
        sendWsMessage({ type: 'automation_set', ...payload });
        if (statusEl) {
            statusEl.className = 'save-status';
            statusEl.innerText = 'Settings broadcasted!';
            if (autoSaveTimer) clearTimeout(autoSaveTimer);
            autoSaveTimer = setTimeout(() => { if (statusEl) statusEl.innerText = ''; }, 2500);
        }
    }
}

async function restartSystem() {
    if (!confirm("Are you sure you want to reboot the controller?")) return;
    try {
        await fetch('/api/system/reboot', { method: 'POST' });
        alert("Reboot command sent! The controller is restarting...");
        setTimeout(() => window.location.reload(), 6000);
    } catch (e) {
        alert("Reboot signal sent. Refreshing page shortly...");
        setTimeout(() => window.location.reload(), 6000);
    }
}

// ==============================================================================
// Wi-Fi Scanning & Configuration
// ==============================================================================

async function scanWifiNetworks() {
    const btn = document.getElementById('btnScanWifi');
    const btnText = document.getElementById('scanBtnText');
    const icon = btn ? btn.querySelector('.scan-icon') : null;
    const box = document.getElementById('wifiNetworksBox');

    if (btn) btn.disabled = true;
    if (btnText) btnText.innerText = 'Scanning...';
    if (icon) icon.classList.add('spinning');
    if (box) box.innerHTML = '<p class="empty-hint">Scanning 2.4GHz Wi-Fi channels...</p>';

    try {
        const res = await fetch('/api/wifi/scan');
        if (res.ok) {
            const data = await res.json();
            renderWifiScanResults(data.networks || []);
        } else {
            if (box) box.innerHTML = '<p class="empty-hint">Failed to scan networks. Please retry.</p>';
        }
    } catch (e) {
        if (box) box.innerHTML = '<p class="empty-hint">Scan request timed out.</p>';
    } finally {
        if (btn) btn.disabled = false;
        if (btnText) btnText.innerText = 'Scan APs';
        if (icon) icon.classList.remove('spinning');
    }
}

function renderWifiScanResults(networks) {
    const box = document.getElementById('wifiNetworksBox');
    if (!box) return;

    if (!networks || networks.length === 0) {
        box.innerHTML = '<p class="empty-hint">No networks discovered nearby.</p>';
        return;
    }

    // Sort networks by signal strength (RSSI descending)
    networks.sort((a, b) => b.rssi - a.rssi);

    box.innerHTML = '';
    networks.forEach(net => {
        if (!net.ssid || net.ssid.trim() === '') return;

        const item = document.createElement('div');
        item.className = 'wifi-net-item';
        item.onclick = () => selectWifiSsid(net.ssid);

        const secureText = net.secure ? '🔒 WPA/WPA2' : '🔓 Open';
        item.innerHTML = `
            <span class="wifi-net-ssid">${escapeHtml(net.ssid)}</span>
            <div class="wifi-net-meta">
                <span>${net.rssi} dBm</span>
                <span>${secureText}</span>
            </div>
        `;
        box.appendChild(item);
    });
}

function selectWifiSsid(ssid) {
    const input = document.getElementById('wifiSsidInput');
    const passInput = document.getElementById('wifiPassInput');
    if (input) {
        input.value = ssid;
    }
    if (passInput) {
        passInput.focus();
    }
}

async function saveWifiConfig() {
    const ssid = document.getElementById('wifiSsidInput').value.trim();
    const pass = document.getElementById('wifiPassInput').value;
    const statusEl = document.getElementById('wifiSaveStatus');

    if (!ssid) {
        if (statusEl) {
            statusEl.className = 'save-status error';
            statusEl.innerText = 'SSID cannot be empty!';
        }
        return;
    }

    if (statusEl) {
        statusEl.className = 'save-status';
        statusEl.innerText = 'Saving & Reconnecting...';
    }

    try {
        const res = await fetch('/api/wifi/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ ssid, pass, reconnect: true })
        });

        if (res.ok) {
            if (statusEl) {
                statusEl.className = 'save-status';
                statusEl.innerText = 'Wi-Fi credentials saved! Connecting...';
            }
            const curSsidEl = document.getElementById('wifiCurSsid');
            if (curSsidEl) curSsidEl.innerText = ssid;
        } else {
            throw new Error('Save failed');
        }
    } catch (e) {
        if (statusEl) {
            statusEl.className = 'save-status error';
            statusEl.innerText = 'Failed to save Wi-Fi configuration.';
        }
    }
}

// ==============================================================================
// Over-The-Air (OTA) Firmware & Filesystem Updates
// ==============================================================================

function initOtaDropZone() {
    const dropZone = document.getElementById('otaDropZone');
    if (!dropZone) return;

    ['dragenter', 'dragover'].forEach(name => {
        dropZone.addEventListener(name, (e) => {
            e.preventDefault();
            dropZone.classList.add('dragover');
        });
    });

    ['dragleave', 'drop'].forEach(name => {
        dropZone.addEventListener(name, (e) => {
            e.preventDefault();
            dropZone.classList.remove('dragover');
        });
    });

    dropZone.addEventListener('drop', (e) => {
        const files = e.dataTransfer.files;
        if (files && files.length > 0) {
            handleOtaFile(files[0]);
        }
    });
}

function handleOtaFileSelected(input) {
    if (input.files && input.files.length > 0) {
        handleOtaFile(input.files[0]);
    }
}

function selectOtaTarget(target) {
    const pillFw = document.getElementById('otaPillFirmware');
    const pillFs = document.getElementById('otaPillFilesystem');
    const rFw = pillFw ? pillFw.querySelector('input') : null;
    const rFs = pillFs ? pillFs.querySelector('input') : null;

    if (target === 'firmware') {
        if (pillFw) pillFw.classList.add('active');
        if (pillFs) pillFs.classList.remove('active');
        if (rFw) rFw.checked = true;
    } else {
        if (pillFs) pillFs.classList.add('active');
        if (pillFw) pillFw.classList.remove('active');
        if (rFs) rFs.checked = true;
    }
    updateOtaReadinessSub();
}

function updateOtaPillSelection() {
    const target = document.querySelector('input[name="otaTarget"]:checked')?.value || 'firmware';
    selectOtaTarget(target);
}

function updateOtaReadinessSub() {
    const target = document.querySelector('input[name="otaTarget"]:checked')?.value || 'firmware';
    const readySub = document.getElementById('otaTileReadySub');
    if (readySub) {
        readySub.innerText = selectedOtaFile 
            ? `Target: ${target === 'firmware' ? 'Firmware' : 'LittleFS'} (verified)`
            : 'System will verify before flashing';
    }
}

function togglePasswordVisibility() {
    const pwdInput = document.getElementById('wifiPassInput');
    const eyeIcon = document.getElementById('eyeIcon');
    if (!pwdInput) return;

    if (pwdInput.type === 'password') {
        pwdInput.type = 'text';
        if (eyeIcon) {
            eyeIcon.innerHTML = `
                <path d="M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0-11-8-11-8a18.45 18.45 0 0 1 5.06-5.94M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1-2.16 3.19m-6.72-1.07a3 3 0 1 1-4.24-4.24"/>
                <line x1="1" y1="1" x2="23" y2="23"/>
            `;
        }
    } else {
        pwdInput.type = 'password';
        if (eyeIcon) {
            eyeIcon.innerHTML = `
                <path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/>
                <circle cx="12" cy="12" r="3"/>
            `;
        }
    }
}

function handleOtaFile(file) {
    if (!file.name.endsWith('.bin')) {
        alert('Please select a valid binary (.bin) image file.');
        return;
    }

    selectedOtaFile = file;
    const nameEl = document.getElementById('otaTileFileName');
    const subEl = document.getElementById('otaTileFileSub');
    const startBtn = document.getElementById('btnStartOta');

    if (nameEl) nameEl.innerText = file.name;
    if (subEl) subEl.innerText = `${(file.size / 1024).toFixed(1)} KB image ready`;
    if (startBtn) startBtn.disabled = false;
    updateOtaReadinessSub();
}

function startOtaUpload() {
    if (!selectedOtaFile) return;

    const target = document.querySelector('input[name="otaTarget"]:checked').value;
    const startBtn = document.getElementById('btnStartOta');
    const progressWrap = document.getElementById('otaProgressContainer');
    const progressBar = document.getElementById('otaProgressBar');
    const progressText = document.getElementById('otaProgressText');
    const bytesText = document.getElementById('otaBytesText');
    const statusMsg = document.getElementById('otaStatusMsg');

    if (startBtn) startBtn.disabled = true;
    if (progressWrap) progressWrap.style.display = 'flex';
    if (statusMsg) {
        statusMsg.className = 'save-status';
        statusMsg.innerText = 'Uploading binary image...';
    }

    const formData = new FormData();
    formData.append('update', selectedOtaFile, selectedOtaFile.name);
    formData.append('target', target);

    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/ota/upload', true);

    xhr.upload.onprogress = (e) => {
        if (e.lengthComputable) {
            const percent = Math.round((e.loaded / e.total) * 100);
            if (progressBar) progressBar.style.width = `${percent}%`;
            if (progressText) progressText.innerText = `Uploading: ${percent}%`;
            if (bytesText) bytesText.innerText = `${(e.loaded / 1024).toFixed(0)} KB / ${(e.total / 1024).toFixed(0)} KB`;
        }
    };

    xhr.onload = () => {
        if (xhr.status === 200) {
            if (progressBar) progressBar.style.width = '100%';
            if (progressText) progressText.innerText = 'Flashing Complete!';
            if (statusMsg) {
                statusMsg.className = 'save-status';
                statusMsg.innerText = 'Update successful! System is rebooting...';
            }
            startRebootCountdown();
        } else {
            if (statusMsg) {
                statusMsg.className = 'save-status error';
                statusMsg.innerText = 'Update failed! ' + xhr.responseText;
            }
            if (startBtn) startBtn.disabled = false;
        }
    };

    xhr.onerror = () => {
        if (statusMsg) {
            statusMsg.className = 'save-status error';
            statusMsg.innerText = 'Network error during upload.';
        }
        if (startBtn) startBtn.disabled = false;
    };

    xhr.send(formData);
}

function startRebootCountdown() {
    let countdown = 8;
    const statusMsg = document.getElementById('otaStatusMsg');
    const timer = setInterval(() => {
        countdown--;
        if (statusMsg) {
            statusMsg.innerText = `Rebooting... reconnecting in ${countdown}s`;
        }
        if (countdown <= 0) {
            clearInterval(timer);
            window.location.reload();
        }
    }, 1000);
}

function escapeHtml(str) {
    return str.replace(/[&<>"']/g, function(m) {
        return {
            '&': '&amp;',
            '<': '&lt;',
            '>': '&gt;',
            '"': '&quot;',
            "'": '&#39;'
        }[m];
    });
}

// ==============================================================================
// Home Assistant & MQTT Integration Logic
// ==============================================================================
function updateHeaderHaIcon(mqtt) {
    const badge = document.getElementById('headerHaBadge');
    if (!badge) return;

    if (mqtt && mqtt.enabled) {
        badge.style.display = 'inline-flex';
        if (mqtt.connected) {
            badge.classList.remove('disconnected');
            badge.title = 'Home Assistant Integration: Connected';
        } else {
            badge.classList.add('disconnected');
            badge.title = 'Home Assistant Integration: Connecting...';
        }
    } else {
        badge.style.display = 'none';
    }
}

function updateMqttStatusBadge(mqtt) {
    const statusVal = document.getElementById('mqttStatusVal');
    if (statusVal) {
        if (!mqtt.enabled) {
            statusVal.innerText = 'Disabled';
            statusVal.style.color = '#94a3b8';
        } else if (mqtt.connected) {
            statusVal.innerText = 'Connected';
            statusVal.style.color = '#10b981';
        } else {
            statusVal.innerText = 'Connecting...';
            statusVal.style.color = '#f59e0b';
        }
    }
    updateHeaderHaIcon(mqtt);
}

async function fetchMqttConfig() {
    try {
        const res = await fetch('/api/mqtt/config');
        if (res.ok) {
            const data = await res.json();
            const enableEl = document.getElementById('mqttEnableToggle');
            const serverEl = document.getElementById('mqttServerInput');
            const portEl = document.getElementById('mqttPortInput');
            const userEl = document.getElementById('mqttUserInput');
            const passEl = document.getElementById('mqttPassInput');
            const nodeIdEl = document.getElementById('mqttNodeIdInput');
            const prefixEl = document.getElementById('mqttPrefixInput');
            const nodeVal = document.getElementById('mqttNodeIdVal');

            if (enableEl) enableEl.checked = !!data.enabled;
            if (serverEl) serverEl.value = data.server || '';
            if (portEl) portEl.value = data.port || 1883;
            if (userEl) userEl.value = data.user || '';
            if (passEl) passEl.value = data.pass || '';
            if (nodeIdEl) nodeIdEl.value = data.node_id || 'room_controller';
            if (prefixEl) prefixEl.value = data.disc_pre || 'homeassistant';
            if (nodeVal) nodeVal.innerText = data.node_id || 'room_controller';

            updateMqttStatusBadge(data);
        }
    } catch (e) {
        console.error('[MQTT] Failed to fetch config:', e);
    }
}

async function saveMqttConfig() {
    const statusEl = document.getElementById('mqttSaveStatus');
    const enabled = document.getElementById('mqttEnableToggle')?.checked || false;
    const server = document.getElementById('mqttServerInput')?.value.trim() || '';
    const port = parseInt(document.getElementById('mqttPortInput')?.value, 10) || 1883;
    const user = document.getElementById('mqttUserInput')?.value.trim() || '';
    const pass = document.getElementById('mqttPassInput')?.value.trim() || '';
    const nodeId = document.getElementById('mqttNodeIdInput')?.value.trim() || 'room_controller';
    const discPre = document.getElementById('mqttPrefixInput')?.value.trim() || 'homeassistant';

    if (statusEl) {
        statusEl.className = 'save-status';
        statusEl.innerText = 'Saving configuration...';
    }

    try {
        const res = await fetch('/api/mqtt/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                enabled: enabled,
                server: server,
                port: port,
                user: user,
                pass: pass,
                node_id: nodeId,
                disc_pre: discPre
            })
        });

        if (res.ok) {
            updateHeaderHaIcon({ enabled: enabled, connected: false });
            if (statusEl) {
                statusEl.className = 'save-status success';
                statusEl.innerText = enabled ? 'Saved! Connecting to broker...' : 'Saved. MQTT disabled.';
                setTimeout(() => { if (statusEl) statusEl.innerText = ''; }, 3500);
            }
            const nodeVal = document.getElementById('mqttNodeIdVal');
            if (nodeVal) nodeVal.innerText = nodeId;
            setTimeout(fetchMqttConfig, 1500);
        } else {
            throw new Error('Server returned error status');
        }
    } catch (e) {
        if (statusEl) {
            statusEl.className = 'save-status error';
            statusEl.innerText = 'Error saving MQTT settings!';
            setTimeout(() => { if (statusEl) statusEl.innerText = ''; }, 3500);
        }
    }
}

async function sendMqttDiscoveryNow() {
    const statusEl = document.getElementById('mqttSaveStatus');
    if (statusEl) {
        statusEl.className = 'save-status';
        statusEl.innerText = 'Publishing discovery payloads...';
    }

    try {
        const res = await fetch('/api/mqtt/discovery', { method: 'POST' });
        const data = await res.json();
        if (res.ok) {
            if (statusEl) {
                statusEl.className = 'save-status success';
                statusEl.innerText = 'Discovery payloads broadcasted to Home Assistant!';
                setTimeout(() => { if (statusEl) statusEl.innerText = ''; }, 4000);
            }
        } else {
            if (statusEl) {
                statusEl.className = 'save-status error';
                statusEl.innerText = data.message || 'Discovery failed: broker not connected';
                setTimeout(() => { if (statusEl) statusEl.innerText = ''; }, 4000);
            }
        }
    } catch (e) {
        if (statusEl) {
            statusEl.className = 'save-status error';
            statusEl.innerText = 'Failed to trigger discovery.';
            setTimeout(() => { if (statusEl) statusEl.innerText = ''; }, 3500);
        }
    }
}

function toggleMqttPasswordVisibility() {
    const pwdInput = document.getElementById('mqttPassInput');
    const eyeIcon = document.getElementById('mqttEyeIcon');
    if (!pwdInput) return;
    if (pwdInput.type === 'password') {
        pwdInput.type = 'text';
        if (eyeIcon) {
            eyeIcon.innerHTML = `
                <path d="M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0-11-8-11-8a18.45 18.45 0 0 1 5.06-5.94M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1-2.16 3.19m-6.72-1.07a3 3 0 1 1-4.24-4.24"/>
                <line x1="1" y1="1" x2="23" y2="23"/>
            `;
        }
    } else {
        pwdInput.type = 'password';
        if (eyeIcon) {
            eyeIcon.innerHTML = `
                <path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/>
                <circle cx="12" cy="12" r="3"/>
            `;
        }
    }
}

