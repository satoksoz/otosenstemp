import { randomBytes, scryptSync, timingSafeEqual } from 'crypto';
import { Pool } from 'pg';

const DEVICE_OFFLINE_AFTER_SECONDS = 30;

function hashPassword(password) {
  const salt = randomBytes(16);
  const derivedKey = scryptSync(password, salt, 64);
  return `${salt.toString('hex')}:${derivedKey.toString('hex')}`;
}

function verifyPassword(password, storedHash) {
  const [saltHex, keyHex] = String(storedHash || '').split(':');
  if (!saltHex || !keyHex) return false;
  const expected = Buffer.from(keyHex, 'hex');
  const actual = scryptSync(password, Buffer.from(saltHex, 'hex'), expected.length);
  return expected.length === actual.length && timingSafeEqual(expected, actual);
}

function mapDevice(row) {
  return {
    id: row.id,
    ownerEmail: row.owner_email,
    zone: row.zone,
    firmware: row.firmware,
    localIp: row.local_ip || '',
    status: row.effective_status || row.status,
    temp: row.temp === null ? null : Number(row.temp),
    lastSeen: row.last_seen || null,
    command: row.command ? JSON.parse(row.command) : null
  };
}

export function createStore(config) {
  const pool = new Pool({
    connectionString: config.connectionString || process.env.DATABASE_URL,
    ssl: process.env.NODE_ENV === 'production' ? { rejectUnauthorized: false } : false
  });

  return {
    async initialize() {
      await pool.query(`
        CREATE TABLE IF NOT EXISTS users (
          email TEXT PRIMARY KEY,
          password_hash TEXT NOT NULL,
          role TEXT NOT NULL CHECK (role IN ('admin', 'user')),
          created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
        );
        CREATE TABLE IF NOT EXISTS devices (
          id TEXT PRIMARY KEY,
          owner_email TEXT REFERENCES users(email) ON DELETE SET NULL,
          zone TEXT NOT NULL DEFAULT 'unassigned',
          firmware TEXT NOT NULL DEFAULT 'unknown',
          local_ip TEXT,
          status TEXT NOT NULL DEFAULT 'offline',
          temp REAL,
          last_seen TIMESTAMPTZ,
          command TEXT
        );
      `);
      await this.createUser(config.adminEmail, config.adminPassword, 'admin', true);
    },

    close() {
      return pool.end();
    },

    async authenticate(email, password) {
      const result = await pool.query('SELECT email, password_hash, role FROM users WHERE email = $1', [email]);
      const user = result.rows[0];
      if (!user || !verifyPassword(password, user.password_hash)) return null;
      return { email: user.email, role: user.role };
    },

    async countUsers() {
      const result = await pool.query('SELECT COUNT(*) AS count FROM users');
      return Number(result.rows[0].count);
    },

    async createUser(email, password, role = 'user', ignoreConflict = false) {
      if (ignoreConflict) {
        const result = await pool.query(
          'INSERT INTO users (email, password_hash, role) VALUES ($1, $2, $3) ON CONFLICT (email) DO NOTHING RETURNING email, role',
          [email, hashPassword(password), role]
        );
        return result.rowCount ? { email, role } : null;
      }

      const result = await pool.query(
        'INSERT INTO users (email, password_hash, role) VALUES ($1, $2, $3) RETURNING email, role',
        [email, hashPassword(password), role]
      );
      return result.rowCount ? { email: result.rows[0].email, role: result.rows[0].role } : null;
    },

    async updateUserPassword(email, password) {
      const result = await pool.query('UPDATE users SET password_hash = $1 WHERE email = $2', [hashPassword(password), email]);
      return result.rowCount > 0;
    },

    async userExists(email) {
      const result = await pool.query('SELECT 1 FROM users WHERE email = $1 LIMIT 1', [email]);
      return result.rowCount > 0;
    },

    async listUsersWithDevices() {
      const usersResult = await pool.query('SELECT email, role, created_at FROM users ORDER BY email');
      const devicesResult = await pool.query('SELECT id, owner_email FROM devices ORDER BY id');
      const devices = devicesResult.rows;

      return usersResult.rows.map((user) => ({
        email: user.email,
        role: user.role,
        createdAt: user.created_at,
        devices: devices.filter((device) => device.owner_email === user.email).map((device) => device.id)
      }));
    },

    async listDevicesForUser(user) {
      const sql = user.role === 'admin'
        ? `
          SELECT *, CASE
            WHEN last_seen IS NULL OR last_seen <= NOW() - INTERVAL '${DEVICE_OFFLINE_AFTER_SECONDS} seconds' THEN 'offline'
            ELSE status
          END AS effective_status
          FROM devices ORDER BY id
        `
        : `
          SELECT *, CASE
            WHEN last_seen IS NULL OR last_seen <= NOW() - INTERVAL '${DEVICE_OFFLINE_AFTER_SECONDS} seconds' THEN 'offline'
            ELSE status
          END AS effective_status
          FROM devices WHERE owner_email = $1 ORDER BY id
        `;
      const params = user.role === 'admin' ? [] : [user.email];
      const result = await pool.query(sql, params);
      return result.rows.map(mapDevice);
    },

    async getDevice(id) {
      const result = await pool.query('SELECT * FROM devices WHERE id = $1', [id]);
      return result.rowCount ? mapDevice(result.rows[0]) : null;
    },

    async createDevice(device) {
      await pool.query(`
        INSERT INTO devices (id, owner_email, zone, firmware, local_ip, status, temp, last_seen, command)
        VALUES ($1, $2, $3, $4, $5, 'offline', NULL, NULL, NULL)
      `, [device.id, device.ownerEmail || null, device.zone, device.firmware, device.localIp || null]);
      return this.getDevice(device.id);
    },

    async updateDeviceOwner(id, ownerEmail) {
      await pool.query('UPDATE devices SET owner_email = $1 WHERE id = $2', [ownerEmail, id]);
      return this.getDevice(id);
    },

    async updateDevice(id, fields) {
      await pool.query(
        'UPDATE devices SET owner_email = $1, zone = $2, firmware = $3, local_ip = $4 WHERE id = $5',
        [fields.ownerEmail || null, fields.zone, fields.firmware, fields.localIp || null, id]
      );
      return this.getDevice(id);
    },

    async renameDevice(id, newId) {
      const client = await pool.connect();
      try {
        await client.query('BEGIN');
        const current = await client.query('SELECT 1 FROM devices WHERE id = $1', [id]);
        if (!current.rowCount) {
          await client.query('ROLLBACK');
          return null;
        }
        const target = await client.query('SELECT 1 FROM devices WHERE id = $1', [newId]);
        if (target.rowCount) {
          await client.query('ROLLBACK');
          return false;
        }
        await client.query('UPDATE devices SET id = $1 WHERE id = $2', [newId, id]);
        await client.query('COMMIT');
        return this.getDevice(newId);
      } catch (error) {
        await client.query('ROLLBACK');
        throw error;
      } finally {
        client.release();
      }
    },

    async deleteDevice(id) {
      const result = await pool.query('DELETE FROM devices WHERE id = $1', [id]);
      return result.rowCount > 0;
    },

    async registerDevice(device) {
      const current = await this.getDevice(device.id);
      if (current) {
        await pool.query(`
          UPDATE devices
          SET zone = $1,
              firmware = $2,
              local_ip = $3,
              status = 'online',
              last_seen = CURRENT_TIMESTAMP
          WHERE id = $4
        `, [device.zone, device.firmware, device.localIp || null, device.id]);
      } else {
        await pool.query(`
          INSERT INTO devices (id, owner_email, zone, firmware, local_ip, status, last_seen)
          VALUES ($1, $2, $3, $4, $5, 'online', CURRENT_TIMESTAMP)
        `, [device.id, device.ownerEmail || null, device.zone, device.firmware, device.localIp || null]);
      }
      return this.getDevice(device.id);
    },

    async updateTelemetry(id, temperature) {
      const result = await pool.query(`
        UPDATE devices
        SET temp = $1,
            status = CASE WHEN $1::REAL >= 85::REAL THEN 'warning' ELSE 'online' END,
            last_seen = CURRENT_TIMESTAMP
        WHERE id = $2
        RETURNING *
      `, [temperature, id]);
      return result.rowCount ? mapDevice(result.rows[0]) : null;
    },

    async queueCommand(id, command) {
      await pool.query('UPDATE devices SET command = $1 WHERE id = $2', [JSON.stringify(command), id]);
      return command;
    },

    async takeNextCommand(id) {
      const client = await pool.connect();
      try {
        await client.query('BEGIN');
        const result = await client.query('SELECT command FROM devices WHERE id = $1 FOR UPDATE', [id]);
        const command = result.rows[0]?.command ? JSON.parse(result.rows[0].command) : null;
        await client.query('UPDATE devices SET command = NULL WHERE id = $1', [id]);
        await client.query('COMMIT');
        return command;
      } catch (error) {
        await client.query('ROLLBACK');
        throw error;
      } finally {
        client.release();
      }
    }
  };
}
