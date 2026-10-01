const API_URL = window.location.origin;
const state = {
  token: localStorage.getItem('mikrodmz_token'),
  dashboard: null,
  socket: null,
  socketReconnectTimer: null,
  socketReconnectAttempt: 0
};

const elements = {
  loginLayer: document.getElementById('login-layer'),
  loginForm: document.getElementById('login-form'),
  formMessage: document.getElementById('form-message'),
  userEmail: document.getElementById('user-email'),
  connectionLabel: document.getElementById('connection-label'),
  deviceList: document.getElementById('device-list'),
  eventLog: document.getElementById('event-log'),
  userForm: document.getElementById('user-form'),
  userFormMessage: document.getElementById('user-form-message'),
  assignDeviceForm: document.getElementById('assign-device-form'),
  assignDeviceMessage: document.getElementById('assign-device-message'),
  otaForm: document.getElementById('ota-form'),
  otaFormMessage: document.getElementById('ota-form-message'),
  deviceForm: document.getElementById('device-form'),
  deviceFormMessage: document.getElementById('device-form-message'),
};

function addEvent(message) {
  const time = new Date().toLocaleTimeString('tr-TR');
  const event = document.createElement('div');
  event.className = 'event';
  event.innerHTML = `<span class="event-time">${time}</span><span>${message}</span>`;
  elements.eventLog.prepend(event);
}

function setText(id, value) {
  document.getElementById(id).textContent = value;
}

async function readApiResponse(response) {
  const text = await response.text();
  try {
    return text ? JSON.parse(text) : {};
  } catch {
    throw new Error(response.status === 404
      ? 'İstenen cihaz veya yönetim endpoint’i bulunamadı. Cihaz listesindeki güncel SAT kimliğini kullanın.'
      : `Sunucu beklenmeyen yanıt verdi (HTTP ${response.status}).`);
  }
}

function renderDashboard(data) {
  state.dashboard = data;
  const { stats, devices, user } = data;
  const isAdmin = user.role === 'admin';
  setText('total-users', stats.totalUsers.toLocaleString('tr-TR'));
  setText('total-devices', stats.totalDevices.toLocaleString('tr-TR'));
  setText('online-devices', stats.onlineDevices.toLocaleString('tr-TR'));
  setText('alerts', stats.alerts.toLocaleString('tr-TR'));
  const onlineRate = stats.totalDevices ? Math.round((stats.onlineDevices / stats.totalDevices) * 100) : 0;
  setText('online-rate', `${onlineRate}% erişilebilirlik`);
  setText('last-update', `Son güncelleme ${new Date().toLocaleTimeString('tr-TR')}`);
  setText('user-email', user.email);
  setText('user-role', isAdmin ? 'Yönetici' : 'Kullanıcı');
  document.getElementById('settings').hidden = !isAdmin;
  document.getElementById('password-management').hidden = !isAdmin;
  document.getElementById('ota').hidden = !isAdmin;
  elements.deviceList.innerHTML = devices.map((device) => `
    <tr><td><a class="device-name" href="${API_URL}/device/${encodeURIComponent(device.id)}/" target="_blank" rel="noreferrer">${device.id}</a><span class="device-id">Web arayüzünü aç</span></td>
    <td>${device.zone}</td><td>${device.temp ? `${device.temp.toFixed(1)} °C` : '--'}</td>
    <td><span class="status ${device.status}">${device.status === 'online' ? 'Çevrimiçi' : device.status === 'warning' ? 'Uyarı' : 'Çevrimdışı'}</span></td>
    <td>${device.status === 'offline' ? 'Bağlantı bekleniyor' : 'Az önce'}</td><td><button type="button" data-device-action="edit" data-device-id="${device.id}">Düzenle</button><button type="button" data-device-action="delete" data-device-id="${device.id}">Sil</button>${isAdmin ? `<button type="button" data-device-action="ota" data-device-id="${device.id}">OTA</button>` : ''}</td></tr>`).join('');
}

async function loadUsers() {
  if (!state.dashboard || state.dashboard.user.role !== 'admin') return;
  const response = await fetch(`${API_URL}/api/users`, { headers: { Authorization: `Bearer ${state.token}` } });
  const result = await readApiResponse(response);
  if (!response.ok) throw new Error(result.error || 'Kullanıcılar alınamadı.');
  document.getElementById('user-list').innerHTML = result.users.map((user) => `<p><strong>${user.email}</strong> (${user.role})<br>${user.devices.length ? user.devices.join(', ') : 'Cihaz atanmadı'}</p>`).join('');
  const accountSelect = document.getElementById('password-account');
  accountSelect.replaceChildren(new Option('Kullanıcı seçin', ''));
  result.users.forEach((user) => accountSelect.add(new Option(`${user.email} (${user.role === 'admin' ? 'Yönetici' : 'Kullanıcı'})`, user.email)));
}

async function loadDashboard(refreshUsers = true) {
  const response = await fetch(`${API_URL}/api/dashboard`, { headers: { Authorization: `Bearer ${state.token}` } });
  if (response.status === 401) throw new Error('Oturum süresi doldu.');
  if (!response.ok) throw new Error('Dashboard verisi alınamadı.');
  renderDashboard(await readApiResponse(response));
  if (refreshUsers) await loadUsers();
}

function connectWebSocket() {
  if (!state.token || [WebSocket.CONNECTING, WebSocket.OPEN].includes(state.socket?.readyState)) return;
  clearTimeout(state.socketReconnectTimer);
  state.socketReconnectTimer = null;

  const socket = new WebSocket(API_URL.replace('http', 'ws') + '/ws');
  state.socket = socket;
  socket.addEventListener('open', () => {
    if (state.socket !== socket) return;
    state.socketReconnectAttempt = 0;
    elements.connectionLabel.textContent = 'BAĞLI';
    addEvent('WebSocket bağlantısı aktif.');
  });
  socket.addEventListener('message', (event) => {
    if (state.socket !== socket) return;
    const data = JSON.parse(event.data);
    if (data.type === 'connected') addEvent(data.message);
  });
  socket.addEventListener('close', () => {
    if (state.socket !== socket) return;
    state.socket = null;
    elements.connectionLabel.textContent = state.token ? 'YENİDEN BAĞLANIYOR' : 'BAĞLANTI KOPTU';
    addEvent('WebSocket bağlantısı kapandı.');
    if (!state.token) return;

    const delay = Math.min(1000 * (2 ** state.socketReconnectAttempt), 30000);
    state.socketReconnectAttempt += 1;
    state.socketReconnectTimer = setTimeout(connectWebSocket, delay);
  });
  socket.addEventListener('error', () => {
    if (state.socket === socket) elements.connectionLabel.textContent = 'YENİDEN BAĞLANIYOR';
  });
}

async function startApp() {
  if (!state.token) return;
  try { await loadDashboard(); elements.loginLayer.classList.add('hidden'); connectWebSocket(); }
  catch (error) { localStorage.removeItem('mikrodmz_token'); state.token = null; elements.formMessage.textContent = error.message; }
}

setInterval(() => {
  if (state.token && state.dashboard && !document.hidden) {
    loadDashboard(false).catch((error) => console.warn('Dashboard yenileme başarısız:', error.message));
  }
}, 10000);

elements.loginForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  elements.formMessage.textContent = 'Giriş doğrulanıyor...';
  const body = { email: document.getElementById('email').value, password: document.getElementById('password').value };
  try {
    const response = await fetch(`${API_URL}/api/login`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
    const result = await readApiResponse(response);
    if (!response.ok) throw new Error(result.error || 'Giriş başarısız.');
    state.token = result.token; localStorage.setItem('mikrodmz_token', state.token); elements.formMessage.textContent = ''; await startApp();
  } catch (error) { elements.formMessage.textContent = error.message; }
});

elements.deviceForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  elements.deviceFormMessage.textContent = 'Cihaz ekleniyor...';
  const body = {
    id: document.getElementById('device-id').value.trim(),
    ownerEmail: document.getElementById('device-owner').value.trim().toLowerCase(),
    zone: document.getElementById('device-zone').value.trim() || 'unassigned',
    firmware: document.getElementById('device-firmware').value.trim() || 'unknown',
    localIp: document.getElementById('device-ip').value.trim(),
  };
  try {
    const response = await fetch(`${API_URL}/api/devices`, {
      method: 'POST',
      headers: { Authorization: `Bearer ${state.token}`, 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
    const result = await readApiResponse(response);
    if (!response.ok) throw new Error(result.error || 'Cihaz eklenemedi.');
    elements.deviceForm.reset();
    document.getElementById('device-zone').value = 'unassigned';
    document.getElementById('device-firmware').value = 'unknown';
    elements.deviceFormMessage.textContent = 'Cihaz eklendi.';
    await loadDashboard();
    addEvent(`${body.id} cihazı eklendi.`);
  } catch (error) {
    elements.deviceFormMessage.textContent = error.message;
  }
});

elements.userForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  elements.userFormMessage.textContent = 'Kullanıcı oluşturuluyor...';
  const body = {
    email: document.getElementById('new-user-email').value.trim().toLowerCase(),
    password: document.getElementById('new-user-password').value,
  };
  try {
    const response = await fetch(`${API_URL}/api/users`, {
      method: 'POST',
      headers: { Authorization: `Bearer ${state.token}`, 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
    const result = await readApiResponse(response);
    if (!response.ok) throw new Error(result.error || 'Kullanıcı oluşturulamadı.');
    elements.userForm.reset();
    elements.userFormMessage.textContent = 'Kullanıcı oluşturuldu.';
    addEvent(`${body.email} kullanıcısı oluşturuldu.`);
  } catch (error) {
    elements.userFormMessage.textContent = error.message;
  }
});

document.getElementById('password-reset-form').addEventListener('submit', async (event) => {
  event.preventDefault();
  const form = event.currentTarget;
  const email = document.getElementById('password-account').value;
  const password = document.getElementById('password-new').value;
  const confirmation = document.getElementById('password-confirm').value;
  const message = document.getElementById('password-reset-message');
  if (password !== confirmation) {
    message.textContent = 'Parolalar eşleşmiyor.';
    return;
  }
  message.textContent = 'Parola güncelleniyor...';
  try {
    const response = await fetch(`${API_URL}/api/users/${encodeURIComponent(email)}/password`, {
      method: 'PATCH',
      headers: { Authorization: `Bearer ${state.token}`, 'Content-Type': 'application/json' },
      body: JSON.stringify({ password }),
    });
    const result = await readApiResponse(response);
    if (!response.ok) throw new Error(result.error || 'Parola değiştirilemedi.');
    form.reset();
    message.textContent = `${email} parolası güncellendi.`;
    addEvent(`${email} parolası yönetici tarafından güncellendi.`);
  } catch (error) {
    message.textContent = error.message;
  }
});

document.getElementById('password-visibility').addEventListener('click', (event) => {
  const inputs = [document.getElementById('password-new'), document.getElementById('password-confirm')];
  const show = inputs[0].type === 'password';
  inputs.forEach((input) => { input.type = show ? 'text' : 'password'; });
  event.currentTarget.textContent = show ? 'Parolaları gizle' : 'Parolaları göster';
});

elements.assignDeviceForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  elements.assignDeviceMessage.textContent = 'Cihaz atanıyor...';
  const deviceId = document.getElementById('assign-device-id').value.trim();
  const ownerEmail = document.getElementById('assign-device-owner').value.trim().toLowerCase();
  try {
    const response = await fetch(`${API_URL}/api/devices/${encodeURIComponent(deviceId)}/owner`, {
      method: 'PATCH',
      headers: { Authorization: `Bearer ${state.token}`, 'Content-Type': 'application/json' },
      body: JSON.stringify({ ownerEmail }),
    });
    const result = await readApiResponse(response);
    if (!response.ok) throw new Error(result.error || 'Cihaz atanamadı.');
    elements.assignDeviceForm.reset();
    elements.assignDeviceMessage.textContent = 'Cihaz kullanıcıya atandı.';
    await loadDashboard();
    addEvent(`${deviceId} cihazı ${ownerEmail} kullanıcısına atandı.`);
  } catch (error) {
    elements.assignDeviceMessage.textContent = error.message;
  }
});

elements.deviceList.addEventListener('click', async (event) => {
  const button = event.target.closest('[data-device-action]');
  if (!button) return;
  const deviceId = button.dataset.deviceId;
  if (button.dataset.deviceAction === 'ota') {
    document.getElementById('ota-device-id').value = deviceId;
    elements.otaFormMessage.textContent = `${deviceId} seçildi. .bin dosyasını seçip Firmware yükle butonuna basın.`;
    document.getElementById('ota').scrollIntoView({ behavior: 'smooth', block: 'center' });
    return;
  }
  if (button.dataset.deviceAction === 'delete') {
    if (!window.confirm(`${deviceId} cihazı silinsin mi?`)) return;
    const response = await fetch(`${API_URL}/api/devices/${encodeURIComponent(deviceId)}`, { method: 'DELETE', headers: { Authorization: `Bearer ${state.token}` } });
    const result = await readApiResponse(response);
    if (!response.ok) return addEvent(result.error || 'Cihaz silinemedi.');
    await loadDashboard();
    addEvent(`${deviceId} cihazı silindi.`);
    return;
  }
  const device = state.dashboard.devices.find((item) => item.id === deviceId);
  const newId = window.prompt('Yeni cihaz adı', deviceId);
  if (!newId) return;
  const ownerEmail = window.prompt('Cihaz sahibi e-postası', device.ownerEmail || '') ?? (device.ownerEmail || '');
  const response = await fetch(`${API_URL}/api/devices/${encodeURIComponent(deviceId)}`, {
    method: 'PATCH',
    headers: { Authorization: `Bearer ${state.token}`, 'Content-Type': 'application/json' },
    body: JSON.stringify({ newId, ownerEmail, zone: device.zone, firmware: device.firmware, localIp: device.localIp }),
  });
  const result = await readApiResponse(response);
  if (!response.ok) return addEvent(result.error || 'Cihaz güncellenemedi.');
  await loadDashboard();
  addEvent(`${deviceId} cihazı güncellendi.`);
});

async function uploadFirmware(deviceId, file, messageElement = elements.otaFormMessage) {
  if (state.dashboard?.user.role !== 'admin') {
    messageElement.textContent = 'Firmware güncellemesi yalnızca yöneticilere açıktır.';
    return;
  }
  if (file.size === 0 || file.size > 4 * 1024 * 1024) {
    messageElement.textContent = 'Firmware boyutu 1 byte ile 4 MB arasında olmalı.';
    return;
  }
  messageElement.textContent = `${deviceId} için firmware yükleniyor...`;
  try {
    const response = await fetch(`${API_URL}/api/devices/${encodeURIComponent(deviceId)}/firmware`, {
      method: 'PUT',
      headers: { Authorization: `Bearer ${state.token}`, 'Content-Type': 'application/octet-stream' },
      body: file,
    });
    const result = await readApiResponse(response);
    if (!response.ok) throw new Error(result.error || 'OTA güncellemesi başarısız.');
    messageElement.textContent = `${deviceId} firmware güncellemesi tamamlandı.`;
    addEvent(`${deviceId} OTA güncellemesi tamamlandı.`);
  } catch (error) {
    messageElement.textContent = error.message;
  }
}

elements.otaForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  const deviceId = document.getElementById('ota-device-id').value.trim();
  const file = document.getElementById('ota-file').files[0];
  if (!file) return;
  await uploadFirmware(deviceId, file);
});

document.getElementById('logout-button').addEventListener('click', () => {
  localStorage.removeItem('mikrodmz_token');
  state.token = null;
  clearTimeout(state.socketReconnectTimer);
  if (state.socket) {
    const socket = state.socket;
    state.socket = null;
    socket.close();
  }
  location.reload();
});
document.getElementById('refresh-button').addEventListener('click', () => loadDashboard().then(() => addEvent('Dashboard verileri yenilendi.')).catch((error) => addEvent(error.message)));
document.getElementById('scan-button').addEventListener('click', (event) => { const button = event.currentTarget; button.textContent = '⌁ Tarama tamamlandı'; addEvent('Cihaz taraması tamamlandı.'); setTimeout(() => { button.textContent = '⌁ Cihazları tara'; }, 1800); });
document.getElementById('clear-button').addEventListener('click', () => { elements.eventLog.innerHTML = ''; });
document.getElementById('export-button').addEventListener('click', () => { const file = new Blob([JSON.stringify(state.dashboard, null, 2)], { type: 'application/json' }); const link = document.createElement('a'); link.href = URL.createObjectURL(file); link.download = 'mikrodmz-dashboard.json'; link.click(); URL.revokeObjectURL(link.href); addEvent('Dashboard özeti indirildi.'); });
startApp();
