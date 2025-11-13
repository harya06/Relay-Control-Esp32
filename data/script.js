// ==================== GLOBAL VARIABLES ====================
let outputs = [];
let currentEditId = null;
let statusPollInterval = null;
let currentCommMode = 1; // Default WS
const TOTAL_OUTPUTS = 20; // KONSTANTA PENTING!

// ==================== HTTP REQUEST HELPER ====================
async function apiRequest(url, method = 'GET', data = null) {
    try {
        const options = {
            method: method,
            headers: {
                'Content-Type': 'application/json'
            }
        };
        
        if (data) {
            options.body = JSON.stringify(data);
        }
        
        const response = await fetch(url, options);
        return await response.json();
    } catch (error) {
        console.error('API Request Error:', error);
        showToast('Koneksi error!', 'error');
        return null;
    }
}

// ==================== STATUS POLLING ====================
async function fetchStatus() {
    const data = await apiRequest('/api/status');
    if (data && data.outputs) {
        outputs = data.outputs;
        currentCommMode = data.commMode;
        
        console.log(`Received ${outputs.length} outputs from server`);
        
        renderOutputs();
        updateWifiStatus(data.wifiConnected);
        updateRemoteStatus(data.remoteConnected);
        updateModeDisplay(data.commMode, data.modeName);
    }
}

function startStatusPolling() {
    fetchStatus();
    statusPollInterval = setInterval(fetchStatus, 500);
}

function stopStatusPolling() {
    if (statusPollInterval) {
        clearInterval(statusPollInterval);
        statusPollInterval = null;
    }
}

// ==================== STATUS UPDATES ====================
function updateWifiStatus(connected) {
    const wifiDot = document.getElementById('wifiStatus');
    if (wifiDot) {
        wifiDot.className = 'status-dot ' + (connected ? 'connected' : 'disconnected');
    }
}

function updateRemoteStatus(connected) {
    const remoteDot = document.getElementById('remoteStatus');
    if (remoteDot) {
        remoteDot.className = 'status-dot ' + (connected ? 'connected' : 'disconnected');
    }
}

function updateModeDisplay(mode, modeName) {
    const modeBadge = document.getElementById('modeBadge');
    const modeText = document.getElementById('modeText');
    
    if (modeBadge && modeText) {
        modeText.textContent = modeName || (mode === 0 ? 'MQTT' : 'WebSocket');
        
        modeBadge.className = 'status-indicator mode-badge';
        if (mode === 0) {
            modeBadge.classList.add('mode-mqtt');
        } else {
            modeBadge.classList.add('mode-ws');
        }
    }
    
    const currentModeEl = document.getElementById('currentMode');
    if (currentModeEl) {
        currentModeEl.textContent = modeName || (mode === 0 ? 'MQTT' : 'WebSocket');
        currentModeEl.className = 'mode-text ' + (mode === 0 ? 'mode-mqtt' : 'mode-ws');
    }
    
    const remoteLabel = document.getElementById('remoteLabel');
    if (remoteLabel) {
        remoteLabel.textContent = mode === 0 ? 'MQTT' : 'WS';
    }
    
    const switchBtn = document.getElementById('switchModeText');
    if (switchBtn) {
        switchBtn.textContent = mode === 0 ? 'Switch to WS' : 'Switch to MQTT';
    }
}

// ==================== MODE SWITCHING ====================
async function switchModeWeb() {
    const currentModeName = currentCommMode === 0 ? 'MQTT' : 'WebSocket';
    const newMode = currentCommMode === 0 ? 1 : 0;
    const newModeName = newMode === 0 ? 'MQTT' : 'WebSocket';
    
    if (confirm(`Switch mode dari ${currentModeName} ke ${newModeName}?\n\nMode akan berubah tanpa restart.`)) {
        showToast('Switching mode...', 'info');
        
        const result = await apiRequest('/api/setmode', 'POST', { mode: newMode });
        
        if (result && result.success) {
            showToast(`✓ Mode switched to ${result.modeName}!`, 'success');
            setTimeout(() => {
                fetchStatus();
            }, 1000);
        } else {
            showToast('Gagal switch mode!', 'error');
        }
    }
}

// ==================== RENDER OUTPUTS ====================
function renderOutputs() {
    const grid = document.getElementById('outputsGrid');
    if (!grid) return;
    
    grid.innerHTML = '';
    
    console.log(`Rendering ${TOTAL_OUTPUTS} output cards...`);
    
    // PASTIKAN render SEMUA 20 output
    for (let i = 0; i < TOTAL_OUTPUTS; i++) {
        const output = outputs[i];
        if (output) {
            console.log(`Output ${i}: ${output.name}, state: ${output.state}`);
            const card = createOutputCard(output, i);
            grid.appendChild(card);
        } else {
            console.warn(`Output ${i} data missing, creating placeholder`);
            const card = createOutputCard(createDefaultOutput(i), i);
            grid.appendChild(card);
        }
    }
    
    console.log(`✓ Rendered ${grid.children.length} cards`);
}

function createDefaultOutput(index) {
    return {
        id: index,
        channel: index + 1,
        name: `Channel ${index + 1}`,
        state: false,
        intervalOn: 5,
        intervalOff: 5,
        autoMode: false
    };
}

function createOutputCard(output, index) {
    const card = document.createElement('div');
    card.className = 'output-card' + (output.state ? ' active' : '');
    
    card.innerHTML = `
        <div class="card-header">
            <div class="card-title">${output.name || 'Channel ' + (index + 1)}</div>
            <div class="card-id">#${index + 1}</div>
        </div>
        
        <div class="card-body">
            <div class="status-badge ${output.state ? 'on' : 'off'}">
                <span class="status-badge-dot"></span>
                ${output.state ? 'ON' : 'OFF'}
            </div>
            
            ${output.autoMode ? '<div class="auto-badge">🔄 AUTO MODE</div>' : ''}
            
            <div class="interval-info">
                <div class="interval-item">
                    <span class="interval-label">ON Time</span>
                    <span class="interval-value">${output.intervalOn || 5}s</span>
                </div>
                <div class="interval-item">
                    <span class="interval-label">OFF Time</span>
                    <span class="interval-value">${output.intervalOff || 5}s</span>
                </div>
            </div>
        </div>
        
        <div class="card-actions">
            <button class="btn-${output.state ? 'danger' : 'success'}" onclick="toggleOutput(${index})">
                ${output.state ? '⏸ OFF' : '▶ ON'}
            </button>
            <button class="btn-edit" onclick="editOutput(${index})" title="Edit">
                ⚙️
            </button>
        </div>
    `;
    
    return card;
}

// ==================== OUTPUT CONTROLS ====================
async function toggleOutput(id) {
    console.log(`Toggle output ${id}`);
    
    // PASTIKAN index valid
    if (id < 0 || id >= TOTAL_OUTPUTS) {
        console.error(`Invalid output ID: ${id}`);
        showToast('ID output tidak valid!', 'error');
        return;
    }
    
    // Ambil state dari outputs array atau default false
    const currentState = outputs[id] ? outputs[id].state : false;
    
    console.log(`Current state: ${currentState}, setting to: ${!currentState}`);
    
    const result = await apiRequest('/api/output', 'POST', {
        action: 'setState',
        id: id,
        state: !currentState
    });
    
    if (result && result.success) {
        console.log(`✓ Output ${id} toggled successfully`);
        setTimeout(fetchStatus, 100);
    } else {
        console.error(`✗ Failed to toggle output ${id}`);
        showToast('Gagal mengubah output!', 'error');
    }
}

function editOutput(id) {
    currentEditId = id;
    const output = outputs[id] || createDefaultOutput(id);
    
    document.getElementById('modalOutputName').textContent = output.name || `Output ${id + 1}`;
    document.getElementById('outputName').value = output.name || '';
    document.getElementById('intervalOn').value = output.intervalOn || 5;
    document.getElementById('intervalOff').value = output.intervalOff || 5;
    document.getElementById('autoMode').checked = output.autoMode || false;
    document.getElementById('maxToggles').value = output.maxToggles || 0;

    const maxDisplay = output.maxToggles > 0 ? output.maxToggles : '∞';
    document.getElementById('currentTogglesDisplay').textContent = 
    `${output.currentToggles || 0} / ${maxDisplay}`;
    
    openModal('editModal');
}

async function saveOutput() {
    if (currentEditId === null) return;
    
    const name = document.getElementById('outputName').value;
    const intervalOn = parseInt(document.getElementById('intervalOn').value);
    const intervalOff = parseInt(document.getElementById('intervalOff').value);
    const autoMode = document.getElementById('autoMode').checked;
    const maxToggles = parseInt(document.getElementById('maxToggles').value);
    
    const currentOutput = outputs[currentEditId] || createDefaultOutput(currentEditId);
    
    // Set name
    if (name !== currentOutput.name) {
        await apiRequest('/api/output', 'POST', {
            action: 'setName',
            id: currentEditId,
            name: name
        });
    }
    
    // Set interval
    if (intervalOn !== currentOutput.intervalOn || intervalOff !== currentOutput.intervalOff) {
        await apiRequest('/api/output', 'POST', {
            action: 'setInterval',
            id: currentEditId,
            intervalOn: intervalOn,
            intervalOff: intervalOff
        });
    }
    
    // Set auto mode
    if (autoMode !== currentOutput.autoMode) {
        await apiRequest('/api/output', 'POST', {
            action: 'setAutoMode',
            id: currentEditId,
            autoMode: autoMode
        });
    }

    // Cek jika nilai maxToggles berubah
    if (maxToggles !== (currentOutput.maxToggles || 0)) {
    await apiRequest('/api/output', 'POST', {
      action: 'setToggleLimit',
      id: currentEditId,
      limit: maxToggles
    });
    // Counter direset oleh backend secara otomatis
  }
    
    closeModal('editModal');
    showToast('Pengaturan disimpan!');
    setTimeout(fetchStatus, 100);
}

async function resetCounter() {
  if (currentEditId === null) return;
  
  const outputName = outputs[currentEditId] ? outputs[currentEditId].name : `Output ${currentEditId + 1}`;

  if (confirm(`Reset meteran perpindahan untuk ${outputName}?`)) {
    const result = await apiRequest('/api/output', 'POST', {
      action: 'resetToggleCounter',
      id: currentEditId
    });

    if (result && result.success) {
      showToast('Meteran berhasil di-reset!', 'success');
      
      // Update tampilan di modal
      const maxDisplay = document.getElementById('maxToggles').value;
      document.getElementById('currentTogglesDisplay').textContent = 
        `0 / ${maxDisplay > 0 ? maxDisplay : '∞'}`;
      
      // Ambil status baru (penting untuk update data di 'outputs' array)
      setTimeout(fetchStatus, 100);
    } else {
      showToast('Gagal reset meteran!', 'error');
    }
  }
}

// ==================== KONTROL SEMUA OUTPUT ====================
async function allOutputsOn() {
    if (confirm(`Nyalakan SEMUA ${TOTAL_OUTPUTS} output dan matikan mode interval/auto?`)) {
        showToast(`Menyalakan ${TOTAL_OUTPUTS} output...`, 'info');
        
        const promises = [];
        
        // 1. Matikan auto mode untuk SEMUA output dulu
        console.log(`Disabling auto mode for ${TOTAL_OUTPUTS} outputs...`);
        for (let i = 0; i < TOTAL_OUTPUTS; i++) {
            promises.push(
                apiRequest('/api/output', 'POST', {
                    action: 'setAutoMode',
                    id: i,
                    autoMode: false
                })
            );
        }
        
        await Promise.all(promises);
        console.log('✓ Auto mode disabled for all');
        
        // 2. Baru nyalakan semua output SERENTAK
        const onPromises = [];
        console.log(`Turning ON ${TOTAL_OUTPUTS} outputs...`);
        for (let i = 0; i < TOTAL_OUTPUTS; i++) {
            onPromises.push(
                apiRequest('/api/output', 'POST', {
                    action: 'setState',
                    id: i,
                    state: true
                })
            );
        }
        
        await Promise.all(onPromises);
        console.log('✓ All outputs turned ON');
        
        showToast(`✓ Semua ${TOTAL_OUTPUTS} output ON, auto mode OFF!`, 'success');
        setTimeout(fetchStatus, 500);
    }
}

async function allOutputsOff() {
    if (confirm(`Matikan SEMUA ${TOTAL_OUTPUTS} output dan matikan mode interval/auto?`)) {
        showToast(`Mematikan ${TOTAL_OUTPUTS} output...`, 'info');
        
        const promises = [];
        
        // 1. Matikan auto mode untuk SEMUA output dulu
        console.log(`Disabling auto mode for ${TOTAL_OUTPUTS} outputs...`);
        for (let i = 0; i < TOTAL_OUTPUTS; i++) {
            promises.push(
                apiRequest('/api/output', 'POST', {
                    action: 'setAutoMode',
                    id: i,
                    autoMode: false
                })
            );
        }
        
        await Promise.all(promises);
        console.log('✓ Auto mode disabled for all');
        
        // 2. Baru matikan semua output SERENTAK
        const offPromises = [];
        console.log(`Turning OFF ${TOTAL_OUTPUTS} outputs...`);
        for (let i = 0; i < TOTAL_OUTPUTS; i++) {
            offPromises.push(
                apiRequest('/api/output', 'POST', {
                    action: 'setState',
                    id: i,
                    state: false
                })
            );
        }
        
        await Promise.all(offPromises);
        console.log('✓ All outputs turned OFF');

        // 4. Trigger rebuild sync groups di ESP32
        console.log('Rebuilding sync groups...');
        await apiRequest('/api/output', 'POST', {
            action: 'rebuildGroups'
        });
        console.log('✓ Sync groups rebuilt');
        
        showToast(`✓ Semua ${TOTAL_OUTPUTS} output OFF, auto mode OFF!`, 'success');
        setTimeout(fetchStatus, 500);
    }
}

// ==================== SET ALL INTERVAL ====================
function openSetAllModal() {
    openModal('setAllModal');
}

async function setAllInterval() {
  const intervalOn = parseInt(document.getElementById('allIntervalOn').value);
  const intervalOff = parseInt(document.getElementById('allIntervalOff').value);

  const maxToggles = parseInt(document.getElementById('allMaxToggles').value);
  
  const autoMode = document.getElementById('allAutoMode').checked;
  const turnOnAll = document.getElementById('turnOnAll').checked;

  let confirmText = `Terapkan ke semua ${TOTAL_OUTPUTS} output:\n`;
  confirmText += `  • Interval ON: ${intervalOn}s\n`;
  confirmText += `  • Interval OFF: ${intervalOff}s\n`; 

  if (maxToggles > 0) {
    confirmText += `  • Batasan: ${maxToggles} kali pindah (Meteran akan di-reset)\n`;
  } else {
    confirmText += `  • Batasan: Tak Terbatas (Meteran akan di-reset)\n`;
  }

  if (autoMode) confirmText += '  ✓ Aktifkan auto mode\n';
  if (turnOnAll) confirmText += '  ✓ Nyalakan semua output\n';
  
  confirmText += '\nLanjutkan?';
  
  if (confirm(confirmText)) {
    showToast(`Menerapkan ke ${TOTAL_OUTPUTS} output...`, 'info');
    
    console.log(`Setting toggle limit to ${maxToggles} for ${TOTAL_OUTPUTS} outputs...`);
    const limitPromises = [];
    for (let i = 0; i < TOTAL_OUTPUTS; i++) {
      limitPromises.push(
        apiRequest('/api/output', 'POST', {
          action: 'setToggleLimit',
          id: i,
          limit: maxToggles
        })
      );
    }
    await Promise.all(limitPromises);
    console.log('✓ Toggle limits set for all outputs (counters reset)');
    
    console.log(`Setting interval for ${TOTAL_OUTPUTS} outputs...`);
    const intervalPromises = [];
    for (let i = 0; i < TOTAL_OUTPUTS; i++) {
      intervalPromises.push(
        apiRequest('/api/output', 'POST', {
          action: 'setInterval',
          id: i,
          intervalOn: intervalOn,
          intervalOff: intervalOff
        })
      );
    }
    await Promise.all(intervalPromises);
    console.log('✓ Intervals set for all outputs');
    
    console.log(`Setting auto mode to ${autoMode} for ${TOTAL_OUTPUTS} outputs...`);
    const autoPromises = [];
    for (let i = 0; i < TOTAL_OUTPUTS; i++) {
      autoPromises.push(
        apiRequest('/api/output', 'POST', {
          action: 'setAutoMode',
          id: i,
          autoMode: autoMode
        })
      );
    }
    await Promise.all(autoPromises);
    console.log('✓ Auto mode set for all outputs');
    
    if (turnOnAll) {
      console.log(`Turning ON ${TOTAL_OUTPUTS} outputs...`);
      const onPromises = [];
      for (let i = 0; i < TOTAL_OUTPUTS; i++) {
        onPromises.push(
          apiRequest('/api/output', 'POST', {
            action: 'setState',
            id: i,
            state: true
          })
        );
      }
      await Promise.all(onPromises);
      console.log('✓ All outputs turned ON');
    }
    
    closeModal('setAllModal');
    showToast(`✓ Pengaturan massal diterapkan ke ${TOTAL_OUTPUTS} output!`, 'success');
    setTimeout(fetchStatus, 500);
  }
}

// ==================== MODAL CONTROLS ====================
function openModal(modalId) {
    const modal = document.getElementById(modalId);
    if (modal) {
        modal.classList.add('active');
    }
}

function closeModal(modalId) {
    const modal = document.getElementById(modalId);
    if (modal) {
        modal.classList.remove('active');
    }
    if (modalId === 'editModal') {
        currentEditId = null;
    }
}

window.onclick = function(event) {
    if (event.target.classList.contains('modal')) {
        event.target.classList.remove('active');
    }
}

// ==================== LOGOUT ====================
function logout() {
    sessionStorage.removeItem('authToken');
    stopStatusPolling();
    window.location.href = '/';
}

// ==================== TOAST NOTIFICATIONS ====================
function showToast(message, type = 'success') {
    const toast = document.createElement('div');
    toast.className = `toast ${type}`;
    toast.textContent = message;
    
    document.body.appendChild(toast);
    
    setTimeout(() => {
        toast.style.animation = 'slideInRight 0.3s reverse';
        setTimeout(() => {
            toast.remove();
        }, 300);
    }, 3000);
}

// ==================== INITIALIZATION ====================
document.addEventListener('DOMContentLoaded', () => {
    const token = sessionStorage.getItem('authToken');
    if (!token && window.location.pathname !== '/') {
        window.location.href = '/';
        return;
    }
    
    if (document.getElementById('outputsGrid')) {
        console.log(`Starting status polling for ${TOTAL_OUTPUTS} outputs...`);
        startStatusPolling();
    }
});

window.addEventListener('beforeunload', () => {
    stopStatusPolling();
});