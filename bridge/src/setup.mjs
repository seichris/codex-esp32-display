import { randomBytes } from 'node:crypto';
import { access, chmod, mkdir, writeFile } from 'node:fs/promises';
import { constants } from 'node:fs';
import { networkInterfaces } from 'node:os';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const configPath = resolve(here, '..', 'config.json');

let configExists = false;
try {
  await access(configPath, constants.F_OK);
  configExists = true;
} catch (error) {
  if (error?.code !== 'ENOENT') throw error;
}

if (configExists) {
  console.error(`Refusing to overwrite existing ${configPath}`);
  process.exitCode = 1;
} else {
  const config = {
    host: '0.0.0.0',
    port: 5180,
    token: randomBytes(32).toString('base64url'),
    pollIntervalMs: 2000,
    maxThreads: 300,
    maxItems: 30,
    codexBin: 'codex',
    codexHome: '~/.codex',
  };

  await mkdir(dirname(configPath), { recursive: true });
  await writeFile(configPath, `${JSON.stringify(config, null, 2)}\n`, { mode: 0o600 });
  // Best effort: some file systems ignore chmod semantics.
  try { await chmod(configPath, 0o600); } catch {}

  const addresses = [];
  for (const entries of Object.values(networkInterfaces())) {
    for (const entry of entries ?? []) {
      if (entry.family === 'IPv4' && !entry.internal) addresses.push(entry.address);
    }
  }

  console.log(`Created ${configPath}`);
  console.log(`Token: ${config.token}`);
  console.log('Put the same token into firmware → Codex Attention → Bridge token.');
  if (addresses.length) {
    for (const address of addresses) {
      console.log(`Candidate bridge URL: http://${address}:${config.port}/api/v1/attention`);
    }
  } else {
    console.log(`Bridge URL: http://<this-mac-lan-ip>:${config.port}/api/v1/attention`);
  }
}
