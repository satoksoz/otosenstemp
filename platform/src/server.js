import express from 'express';
import cors from 'cors';
import helmet from 'helmet';
import dotenv from 'dotenv';
import http from 'http';
import { createServer } from 'http';
import { WebSocketServer } from 'ws';
import jwt from 'jsonwebtoken';
import { randomUUID } from 'crypto';
import { dirname, resolve } from 'path';
import { fileURLToPath } from 'url';
import { createStore } from './store.js';

dotenv.config({ path: resolve(dirname(fileURLToPath(import.meta.url)), '../.env') });

const app = express();
const server = createServer(app);
const wsServer = new WebSocketServer({ noServer: true });
const ownTunnelServer = new WebSocketServer({ noServer: true });
const isProduction = process.env.NODE_ENV === 'production';
const PORT = Number(process.env.PORT || 3000);
const JWT_SECRET = process.env.JWT_SECRET || (isProduction ? '' : 'dev-secret');
const DEVICE_API_KEY = process.env.DEVICE_API_KEY || (isProduction ? '' : 'dev-device-key');
const TUNNEL_TOKEN = process.env.TUNNEL_TOKEN || (isProduction ? '' : 'mikrodmz-local-tunnel-key-2026');
const PUBLIC_BASE_URL = process.env.PUBLIC_BASE_URL || 'http://localhost:3000';
const ADMIN_EMAIL = process.env.ADMIN_EMAIL || 'admin@mikrodmz.com';
const ADMIN_PASSWORD = process.env.ADMIN_PASSWORD || (isProduction ? '' : 'admin1234567');
const DB_CONFIG = {
  connectionString: process.env.DATABASE_URL || '',
  adminEmail: ADMIN_EMAIL,
  adminPassword: ADMIN_PASSWORD
};
const WEB_DIR = resolve(dirname(fileURLToPath(import.meta.url)), '../../web');
const CORS_ORIGINS = (process.env.CORS_ORIGINS || 'http://localhost:3000,http://localhost:8000,http://127.0.0.1:8000,http://192.168.137.1:8000')
  .split(',')
  .map((origin) => origin.trim())
  .filter(Boolean);

function requireProductionSecret(name, value, minimumLength = 16) {
  if (isProduction && (!value || value.length < minimumLength)) {
    throw new Error(`${name} must be set to at least ${minimumLength} characters in production`);
  }
}

requireProductionSecret('JWT_SECRET', JWT_SECRET, 32);
requireProductionSecret('DEVICE_API_KEY', DEVICE_API_KEY);
requireProductionSecret('TUNNEL_TOKEN', TUNNEL_TOKEN);
if (isProduction && (!ADMIN_PASSWORD || ADMIN_PASSWORD.length < 12)) {
  throw new Error('ADMIN_PASSWORD must be set to at least 12 characters in production');
}

if (!isProduction) console.warn('Development authentication defaults are active; set NODE_ENV=production before deployment.');

const store = createStore(DB_CONFIG);
const tunnelDevices = new Map();
const tunnelStreams = new Map();

function sendTunnelEnvelope(socket, type, connectionId, data) {
  if (socket.readyState !== 1) return;
  const envelope = { t: type };
  if (connectionId) envelope.c = connectionId;
  if (data !== undefined) envelope.d = data;
  socket.send(JSON.stringify(envelope));
}

function deviceIdFromRequest(req) {
  const hostname = (req.headers.host || '').split(':')[0];
  const hostMatch = hostname.match(/^([a-z0-9-]+)\.device\./i);
  if (hostMatch) return hostMatch[1];
  const pathMatch = req.url.match(/^\/device\/([^/]+)(\/.*)?$/);
  return pathMatch ? decodeURIComponent(pathMatch[1]) : '';
}

function pathForDeviceRequest(req) {
  const pathMatch = req.url.match(/^\/device\/[^/]+(\/.*)?$/);
  return pathMatch?.[1] || '/';
}

function gatewayScript(deviceId) {
  const prefix = `/device/${encodeURIComponent(deviceId)}`;
  return `<script>(function(){const p=${JSON.stringify(prefix)};const f=window.fetch;window.fetch=function(input,...args){if(typeof input==='string'&&input.startsWith('/')&&!input.startsWith(p+'/'))input=p+input;return f.call(this,input,...args)};window.WebSocket=function(){const s=this;s.readyState=0;setTimeout(function(){s.readyState=1;if(typeof s.onopen==='function')s.onopen()},0);this.send=function(){};this.close=function(){s.readyState=3;if(typeof s.onclose==='function')s.onclose()}};window.WebSocket.OPEN=1;window.WebSocket.CLOSED=3;})();</script>`;
}

function rewriteDeviceHtml(body, deviceId) {
  const prefix = `/device/${encodeURIComponent(deviceId)}`;
  return body
    .replaceAll('href="/', `href="${prefix}/`)
    .replaceAll("href='/'", `href='${prefix}/'`)
    .replaceAll('action="/', `action="${prefix}/`)
    .replaceAll("action='/'", `action='${prefix}/'`)
    .replaceAll("fetch('/", `fetch('${prefix}/`)
    .replaceAll('fetch("/', `fetch("${prefix}/`);
}

function responseHeaderSeparator(buffer) {
  const crlfSeparator = buffer.indexOf('\r\n\r\n');
  if (crlfSeparator >= 0) return { index: crlfSeparator, length: 4 };
  const lfSeparator = buffer.indexOf('\n\n');
  if (lfSeparator >= 0) return { index: lfSeparator, length: 2 };
  return null;
}

function finishDeviceResponse(stream) {
  const separator = responseHeaderSeparator(stream.responseBuffer);
  if (!separator) {
    console.warn(`Invalid device response from ${stream.deviceId}: missing HTTP headers`);
    return stream.res.status(502).end('Invalid device response');
  }
  const headerText = stream.responseBuffer.subarray(0, separator.index).toString('latin1');
  const lines = headerText.split(/\r?\n/);
  const statusLine = lines.shift()?.replace(/^\uFEFF/, '').trim() || '';
  const statusMatch = statusLine.match(/^HTTP\/\d\.\d\s+(\d{3})(?:\s|$)/);
  if (!statusMatch) {
    console.warn(`Invalid device status from ${stream.deviceId}: ${JSON.stringify(statusLine)}`);
    return stream.res.status(502).end('Invalid device status');
  }
  const headers = {};
  lines.forEach((line) => {
    const colon = line.indexOf(':');
    if (colon > 0) headers[line.slice(0, colon).trim()] = line.slice(colon + 1).trim();
  });
  delete headers.Connection;
  delete headers['Keep-Alive'];
  let body = stream.responseBuffer.subarray(separator.index + separator.length);
  const contentType = Object.entries(headers).find(([name]) => name.toLowerCase() === 'content-type')?.[1] || '';
  if (String(contentType).toLowerCase().includes('text/html')) {
    body = Buffer.from(rewriteDeviceHtml(body.toString('utf8'), stream.deviceId).replace('</head>', `${gatewayScript(stream.deviceId)}</head>`));
    delete headers['Content-Encoding'];
    headers['Cache-Control'] = 'no-store, no-cache, must-revalidate';
    headers.Pragma = 'no-cache';
  }
  headers['Content-Length'] = body.length;
  stream.res.writeHead(Number(statusMatch[1]), headers);
  stream.res.end(body);
}

function relayDeviceResponse(stream, bytes) {
  stream.responseBuffer = Buffer.concat([stream.responseBuffer, bytes]);
  const separator = responseHeaderSeparator(stream.responseBuffer);
  if (!separator) return;
  const headerText = stream.responseBuffer.subarray(0, separator.index).toString('latin1');
  const contentLengthLine = headerText.split(/\r?\n/).find((line) => line.toLowerCase().startsWith('content-length:'));
  if (!contentLengthLine) return;
  const expectedLength = Number(contentLengthLine.split(':')[1].trim());
  const actualLength = stream.responseBuffer.length - separator.index - separator.length;
  if (Number.isFinite(expectedLength) && actualLength >= expectedLength) {
    finishDeviceResponse(stream);
    stream.completed = true;
  }
}

function isLanIpv4(value) {
  const octets = String(value || '').split('.').map(Number);
  if (octets.length !== 4 || octets.some((octet) => !Number.isInteger(octet) || octet < 0 || octet > 255)) return false;
  if (octets[0] === 10 || octets[0] === 192 && octets[1] === 168) return true;
  return octets[0] === 172 && octets[1] >= 16 && octets[1] <= 31;
}

function relayDirectHttpRequest(req, res, deviceId, localIp) {
  const headers = Object.fromEntries(
    Object.entries(req.headers).filter(([name]) => name !== 'host' && name !== 'connection')
  );
  const request = http.request({
    host: localIp,
    port: 80,
    method: req.method,
    path: pathForDeviceRequest(req),
    headers: { ...headers, host: localIp, connection: 'close' },
    timeout: 15000
  }, (deviceResponse) => {
    const chunks = [];
    deviceResponse.on('data', (chunk) => chunks.push(chunk));
    deviceResponse.on('end', () => {
      const responseHeaders = { ...deviceResponse.headers };
      let body = Buffer.concat(chunks);
      const contentType = String(responseHeaders['content-type'] || '').toLowerCase();
      if (contentType.includes('text/html')) {
        body = Buffer.from(rewriteDeviceHtml(body.toString('utf8'), deviceId).replace('</head>', `${gatewayScript(deviceId)}</head>`));
        delete responseHeaders['content-encoding'];
        responseHeaders['cache-control'] = 'no-store, no-cache, must-revalidate';
        responseHeaders.pragma = 'no-cache';
      }
      delete responseHeaders.connection;
      delete responseHeaders['keep-alive'];
      responseHeaders['content-length'] = body.length;
      res.writeHead(deviceResponse.statusCode || 502, responseHeaders);
      res.end(body);
    });
  });
  request.on('timeout', () => request.destroy(new Error('device request timeout')));
  request.on('error', (error) => {
    if (!res.headersSent) res.status(502).json({ error: `Cihaz LAN bağlantısı başarısız: ${error.message}` });
  });
  req.pipe(request);
}

function relayFirmwareUpload(req, res, deviceId, localIp) {
  const contentLength = Number(req.headers['content-length'] || 0);
  if (!Number.isFinite(contentLength) || contentLength <= 0 || contentLength > 4 * 1024 * 1024) {
    return res.status(413).json({ error: 'Firmware boyutu 1 byte ile 4 MB arasında olmalı' });
  }
  const request = http.request({
    host: localIp,
    port: 80,
    method: 'POST',
    path: '/update',
    headers: { 'content-type': 'application/octet-stream', 'content-length': contentLength, connection: 'close' },
    timeout: 120000
  }, (deviceResponse) => {
    const chunks = [];
    deviceResponse.on('data', (chunk) => chunks.push(chunk));
    deviceResponse.on('end', () => {
      const body = Buffer.concat(chunks).toString('utf8');
      let result;
      try { result = JSON.parse(body); } catch { result = { error: body || 'Cihaz OTA yanıtı geçersiz' }; }
      return res.status(deviceResponse.statusCode || 502).json(result);
    });
  });
  request.on('timeout', () => request.destroy(new Error('OTA upload timeout')));
  request.on('error', (error) => {
    if (!res.headersSent) res.status(502).json({ error: `OTA cihaz bağlantısı başarısız: ${error.message}` });
  });
  req.pipe(request);
}

async function relayHttpRequest(req, res, deviceId, relayOptions = {}) {
  const device = await store.getDevice(deviceId);
  const tunnel = tunnelDevices.get(deviceId);
  if (!tunnel) {
    if (!isProduction && device?.localIp && isLanIpv4(device.localIp)) {
      return relayDirectHttpRequest(req, res, deviceId, device.localIp);
    }
    return res.status(502).json({ error: 'Cihaz tüneli bağlı değil' });
  }
  const connectionId = randomUUID();
  const stream = { res, socket: tunnel.socket, deviceId, responseBuffer: Buffer.alloc(0) };
  stream.timeout = setTimeout(() => {
    if (!stream.completed && !res.headersSent) res.status(504).json({ error: 'ESP32 yanıt zaman aşımına uğradı' });
    tunnelStreams.delete(connectionId);
  }, relayOptions.timeoutMs || 15000);
  tunnelStreams.set(connectionId, stream);
  const headers = Object.entries(req.headers)
    .filter(([name]) => name !== 'host' && name !== 'connection')
    .map(([name, value]) => `${name}: ${Array.isArray(value) ? value.join(', ') : value}`)
    .join('\r\n');
  const method = relayOptions.method || req.method;
  const requestPath = relayOptions.path || pathForDeviceRequest(req);
  const rawRequest = `${method} ${requestPath} HTTP/1.1\r\nHost: localhost\r\n${headers}\r\nConnection: close\r\n\r\n`;
  sendTunnelEnvelope(tunnel.socket, 'new_conn', connectionId, { local_port: 80 });
  sendTunnelEnvelope(tunnel.socket, 'data', connectionId, Buffer.from(rawRequest).toString('base64'));
  req.on('data', (chunk) => sendTunnelEnvelope(tunnel.socket, 'data', connectionId, Buffer.from(chunk).toString('base64')));
}

ownTunnelServer.on('connection', (socket) => {
  let tunnel = null;
  console.info('Device tunnel WebSocket connected');
  socket.on('message', (message) => {
    let envelope;
    try { envelope = JSON.parse(message.toString()); } catch { return; }
    if (envelope.t === 'hello' && !tunnel) {
      const hello = envelope.d || {};
      if (hello.token !== TUNNEL_TOKEN || !hello.device_id) {
        console.warn(`Device tunnel authentication rejected for ${hello.device_id || 'unknown device'}`);
        return socket.close(1008, 'invalid tunnel credentials');
      }
      tunnel = { socket, deviceId: hello.device_id };
      tunnelDevices.set(hello.device_id, tunnel);
      console.info(`Device tunnel authenticated for ${hello.device_id}`);
      sendTunnelEnvelope(socket, 'ready', '', { public_url: `${PUBLIC_BASE_URL}/device/${encodeURIComponent(hello.device_id)}`, port: 80 });
      return;
    }
    if (envelope.t === 'data' && envelope.c) {
      const stream = tunnelStreams.get(envelope.c);
      if (stream) relayDeviceResponse(stream, Buffer.from(envelope.d || '', 'base64'));
    }
    if (envelope.t === 'conn_close' && envelope.c) {
      const stream = tunnelStreams.get(envelope.c);
      if (stream) {
        if (!stream.completed) finishDeviceResponse(stream);
        clearTimeout(stream.timeout);
        tunnelStreams.delete(envelope.c);
      }
    }
    if (envelope.t === 'ping') sendTunnelEnvelope(socket, 'pong');
  });
  socket.on('close', (code, reason) => {
    console.info(`Device tunnel WebSocket closed for ${tunnel?.deviceId || 'unauthenticated client'}: ${code} ${reason.toString()}`);
    if (!tunnel) return;
    if (tunnelDevices.get(tunnel.deviceId)?.socket === socket) tunnelDevices.delete(tunnel.deviceId);
    for (const [id, stream] of tunnelStreams) {
      if (stream.socket === socket) { if (!stream.res.headersSent) stream.res.end(); tunnelStreams.delete(id); }
    }
  });
  socket.on('error', (error) => {
    console.warn(`Device tunnel WebSocket error for ${tunnel?.deviceId || 'unauthenticated client'}: ${error.message}`);
  });
});

server.on('upgrade', (request, socket, head) => {
  const pathname = new URL(request.url, 'http://localhost').pathname;
  if (pathname === '/ws') return wsServer.handleUpgrade(request, socket, head, (client) => wsServer.emit('connection', client, request));
  if (pathname === '/ws/device') return ownTunnelServer.handleUpgrade(request, socket, head, (client) => ownTunnelServer.emit('connection', client, request));
  socket.destroy();
});

app.use(cors({ origin: CORS_ORIGINS }));
app.use(helmet({
  contentSecurityPolicy: {
    directives: {
      upgradeInsecureRequests: isProduction ? [] : null
    }
  },
  crossOriginOpenerPolicy: isProduction ? undefined : false,
  originAgentCluster: isProduction,
  strictTransportSecurity: isProduction ? undefined : false
}));
app.use((req, res, next) => {
  const deviceId = deviceIdFromRequest(req);
  if (!deviceId) return next();
  res.setHeader(
    'Content-Security-Policy',
    "default-src 'self'; base-uri 'self'; font-src 'self' https: data:; form-action 'self'; frame-ancestors 'self'; img-src 'self' data:; object-src 'none'; script-src 'self' 'unsafe-inline'; script-src-attr 'unsafe-inline'; style-src 'self' https: 'unsafe-inline'"
  );
  return relayHttpRequest(req, res, deviceId).catch((error) => {
    console.error(`Device proxy failed for ${deviceId}:`, error.message);
    if (!res.headersSent) res.status(502).json({ error: 'Cihaz bağlantısı başarısız' });
  });
});
app.use(express.json({ limit: '2mb' }));
app.use(express.static(WEB_DIR, { index: false }));

function requireAuth(req, res, next) {
  const auth = req.headers.authorization || '';
  const token = auth.startsWith('Bearer ') ? auth.slice(7) : '';

  if (!token) return res.status(401).json({ error: 'Token gerekli' });

  try {
    req.user = jwt.verify(token, JWT_SECRET);
    return next();
  } catch (err) {
    return res.status(401).json({ error: 'Token geçersiz' });
  }
}

function requireAdmin(req, res, next) {
  return requireAuth(req, res, () => {
    if (req.user.role !== 'admin') return res.status(403).json({ error: 'Yönetici yetkisi gerekli' });
    return next();
  });
}

function publicDevice(device) {
  const { command, ...safeDevice } = device;
  return safeDevice;
}

function requireDevice(req, res, next) {
  if (req.headers['x-device-key'] !== DEVICE_API_KEY) {
    return res.status(401).json({ error: 'Cihaz anahtarı geçersiz' });
  }

  const deviceId = req.headers['x-device-id'] || req.body?.deviceId;
  if (!deviceId) return res.status(400).json({ error: 'Cihaz kimliği gerekli' });
  req.deviceId = deviceId;
  return next();
}

function broadcast(message) {
  const payload = JSON.stringify(message);
  wsServer.clients.forEach((client) => {
    if (client.readyState === 1) client.send(payload);
  });
}

app.get('/', (req, res) => {
  res.sendFile(resolve(WEB_DIR, 'admin.html'));
});

app.get('/health', (req, res) => {
  res.json({ ok: true, service: 'MikroDMZ Platform', time: new Date().toISOString() });
});

app.post('/api/login', async (req, res) => {
  const { email, password } = req.body || {};

  if (!email || !password) {
    return res.status(400).json({ error: 'Email ve şifre gerekli' });
  }

  const user = await store.authenticate(email, password);
  if (user) {
    const token = jwt.sign({ email: user.email, role: user.role }, JWT_SECRET, { expiresIn: '1h' });
    return res.json({ token, user });
  }

  return res.status(401).json({ error: 'Geçersiz giriş' });
});

app.get('/api/dashboard', requireAuth, async (req, res) => {
  const deviceList = (await store.listDevicesForUser(req.user)).map(publicDevice);
  const onlineDevices = deviceList.filter((device) => device.status !== 'offline').length;
  const alerts = deviceList.filter((device) => device.status === 'warning').length;

  return res.json({
    user: req.user,
    stats: { totalUsers: await store.countUsers(), totalDevices: deviceList.length, onlineDevices, alerts },
    devices: deviceList
  });
});

app.get('/api/devices', requireAuth, async (req, res) => {
  return res.json({ devices: (await store.listDevicesForUser(req.user)).map(publicDevice) });
});

app.post('/api/users', requireAdmin, async (req, res) => {
  const email = String(req.body?.email || '').trim().toLowerCase();
  const password = String(req.body?.password || '');
  if (!/^\S+@\S+\.\S+$/.test(email)) return res.status(400).json({ error: 'Geçerli bir e-posta gerekli' });
  if (password.length < 8) return res.status(400).json({ error: 'Şifre en az 8 karakter olmalı' });
  if (await store.userExists(email)) return res.status(409).json({ error: 'Bu kullanıcı zaten kayıtlı' });
  const user = await store.createUser(email, password);
  return res.status(201).json({ ok: true, user });
});

app.get('/api/users', requireAdmin, async (req, res) => {
  return res.json({ users: await store.listUsersWithDevices() });
});

app.patch('/api/users/:email/password', requireAdmin, async (req, res) => {
  const email = String(req.params.email || '').trim().toLowerCase();
  const password = String(req.body?.password || '');
  if (password.length < 8) return res.status(400).json({ error: 'Şifre en az 8 karakter olmalı' });
  if (!await store.updateUserPassword(email, password)) return res.status(404).json({ error: 'Kullanıcı bulunamadı' });
  return res.json({ ok: true, user: { email } });
});

app.post('/api/devices', requireAdmin, async (req, res) => {
  const { id, zone = 'unassigned', firmware = 'unknown', localIp = '', ownerEmail = '' } = req.body || {};
  if (!id || !/^[a-zA-Z0-9_-]{2,64}$/.test(id)) {
    return res.status(400).json({ error: 'Geçerli bir cihaz kimliği gerekli' });
  }
  if (await store.getDevice(id)) return res.status(409).json({ error: 'Bu cihaz zaten kayıtlı' });
  if (localIp && !isLanIpv4(localIp)) return res.status(400).json({ error: 'Geçerli bir LAN IPv4 adresi gerekli' });
  if (ownerEmail && !await store.userExists(ownerEmail)) return res.status(400).json({ error: 'Cihaz sahibi kullanıcı bulunamadı' });

  const device = {
    id,
    ownerEmail: ownerEmail || null,
    zone,
    firmware,
    localIp,
    status: 'offline',
    temp: null,
    lastSeen: null,
    command: null
  };
  const created = await store.createDevice(device);
  broadcast({ type: 'device:updated', device: publicDevice(created) });
  return res.status(201).json({ ok: true, device: publicDevice(created) });
});

app.patch('/api/devices/:id/owner', requireAdmin, async (req, res) => {
  const ownerEmail = String(req.body?.ownerEmail || '').trim().toLowerCase();
  if (!ownerEmail || !await store.userExists(ownerEmail)) {
    return res.status(400).json({ error: 'Cihaz sahibi kullanıcı bulunamadı' });
  }
  const updated = await store.updateDeviceOwner(req.params.id, ownerEmail);
  if (!updated) return res.status(404).json({ error: 'Cihaz bulunamadı' });
  broadcast({ type: 'device:updated', device: publicDevice(updated) });
  return res.json({ ok: true, device: publicDevice(updated) });
});

app.put('/api/devices/:id/firmware', requireAdmin, async (req, res) => {
  const device = await store.getDevice(req.params.id);
  if (!device) return res.status(404).json({ error: 'Cihaz bulunamadı' });
  if (!String(req.headers['content-type'] || '').toLowerCase().startsWith('application/octet-stream')) {
    return res.status(415).json({ error: 'Firmware application/octet-stream olarak gönderilmeli' });
  }
  const contentLength = Number(req.headers['content-length'] || 0);
  if (!Number.isFinite(contentLength) || contentLength <= 0 || contentLength > 4 * 1024 * 1024) {
    return res.status(413).json({ error: 'Firmware boyutu 1 byte ile 4 MB arasında olmalı' });
  }
  if (!isProduction && device.localIp && isLanIpv4(device.localIp)) {
    return relayFirmwareUpload(req, res, device.id, device.localIp);
  }
  return relayHttpRequest(req, res, device.id, { method: 'POST', path: '/update', timeoutMs: 120000 });
});

app.patch('/api/devices/:id', requireAdmin, async (req, res) => {
  const current = await store.getDevice(req.params.id);
  if (!current) return res.status(404).json({ error: 'Cihaz bulunamadı' });
  const newId = String(req.body?.newId || req.params.id).trim();
  const ownerEmail = String(req.body?.ownerEmail ?? current.ownerEmail ?? '').trim().toLowerCase();
  const zone = String(req.body?.zone ?? current.zone).trim() || 'unassigned';
  const firmware = String(req.body?.firmware ?? current.firmware).trim() || 'unknown';
  const localIp = String(req.body?.localIp ?? current.localIp).trim();
  if (!/^[a-zA-Z0-9_-]{2,64}$/.test(newId)) return res.status(400).json({ error: 'Geçerli bir cihaz kimliği gerekli' });
  if (localIp && !isLanIpv4(localIp)) return res.status(400).json({ error: 'Geçerli bir LAN IPv4 adresi gerekli' });
  if (ownerEmail && !await store.userExists(ownerEmail)) return res.status(400).json({ error: 'Cihaz sahibi kullanıcı bulunamadı' });

  const renamed = newId === req.params.id ? current : await store.renameDevice(req.params.id, newId);
  if (renamed === false) return res.status(409).json({ error: 'Yeni cihaz kimliği zaten kullanılıyor' });
  const updated = await store.updateDevice(newId, { ownerEmail, zone, firmware, localIp });
  broadcast({ type: 'device:updated', device: publicDevice(updated) });
  return res.json({ ok: true, device: publicDevice(updated) });
});

app.delete('/api/devices/:id', requireAdmin, async (req, res) => {
  if (!await store.deleteDevice(req.params.id)) return res.status(404).json({ error: 'Cihaz bulunamadı' });
  broadcast({ type: 'device:deleted', deviceId: req.params.id });
  return res.json({ ok: true });
});

app.post('/api/devices/register', requireDevice, async (req, res) => {
  const { zone = 'unassigned', firmware = 'unknown', localIp = '' } = req.body || {};
  if (localIp && !isLanIpv4(localIp)) return res.status(400).json({ error: 'Geçerli bir LAN IPv4 adresi gerekli' });
  const current = await store.getDevice(req.deviceId);
  const device = await store.registerDevice({ id: req.deviceId, ownerEmail: current?.ownerEmail || ADMIN_EMAIL, zone, firmware, localIp });
  broadcast({ type: 'device:updated', device: publicDevice(device) });
  return res.status(201).json({ ok: true, device: publicDevice(device) });
});

app.post('/api/devices/:id/telemetry', requireDevice, async (req, res) => {
  if (req.params.id !== req.deviceId) return res.status(403).json({ error: 'Cihaz kimliği eşleşmiyor' });
  const temperature = Number(req.body?.temperature);
  if (!Number.isFinite(temperature)) return res.status(400).json({ error: 'Geçerli sıcaklık gerekli' });

  const updated = await store.updateTelemetry(req.deviceId, temperature);
  if (!updated) return res.status(404).json({ error: 'Cihaz bulunamadı' });
  broadcast({ type: 'telemetry', device: publicDevice(updated) });
  return res.json({ ok: true, device: publicDevice(updated) });
});

app.post('/api/devices/:id/command', requireAuth, async (req, res) => {
  const device = await store.getDevice(req.params.id);
  if (!device) return res.status(404).json({ error: 'Cihaz bulunamadı' });
  if (req.user.role !== 'admin' && device.ownerEmail !== req.user.email) {
    return res.status(403).json({ error: 'Bu cihaz için yetkiniz yok' });
  }
  if (!req.body?.command) return res.status(400).json({ error: 'Komut gerekli' });

  const command = { name: req.body.command, payload: req.body.payload || {}, createdAt: new Date().toISOString() };
  const queued = await store.queueCommand(device.id, command);
  broadcast({ type: 'command:queued', deviceId: device.id, command: queued });
  return res.status(202).json({ ok: true, command: queued });
});

app.get('/api/devices/:id/commands/next', requireDevice, async (req, res) => {
  if (req.params.id !== req.deviceId) return res.status(403).json({ error: 'Cihaz kimliği eşleşmiyor' });
  const device = await store.getDevice(req.deviceId);
  if (!device) return res.status(404).json({ error: 'Cihaz bulunamadı' });

  try {
    const command = await store.takeNextCommand(req.deviceId);
    return res.json({ command });
  } catch (error) {
    console.error(`Command dequeue failed for ${req.deviceId}:`, error.code || error.message);
    return res.status(503).json({ error: 'Komut kuyruğu geçici olarak meşgul, tekrar deneyin' });
  }
});

wsServer.on('connection', (socket) => {
  socket.send(JSON.stringify({ type: 'connected', message: 'MikroDMZ websocket bağlandı' }));

  socket.on('message', (msg) => {
    const text = msg.toString();
    socket.send(JSON.stringify({ type: 'echo', data: text }));
  });
});

try {
  await store.initialize();
  server.listen(PORT, '0.0.0.0', () => {
    console.log(`MikroDMZ platform server started on http://localhost:${PORT} (listening on all interfaces)`);
  });
} catch (error) {
  console.error('Database initialization failed:', error.code || error.message || error);
  process.exitCode = 1;
}
