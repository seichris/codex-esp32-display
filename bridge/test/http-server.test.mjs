import test from 'node:test';
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { createBridgeServer } from '../src/http-server.mjs';

async function withServer(callback) {
  const service = {
    connected: true,
    snapshot: { version: 1, generatedAt: 'now', count: 0, totalCount: 0, items: [], diagnostics: {} },
    async refresh() { return this.snapshot; },
  };
  const server = createBridgeServer({ service, token: 'abcdefghijklmnopqrstuvwxyz123456' });
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const { port } = server.address();
  try { await callback(`http://127.0.0.1:${port}`); }
  finally { await new Promise((resolve) => server.close(resolve)); }
}

test('health is public while attention data requires bearer token', async () => {
  await withServer(async (base) => {
    assert.equal((await fetch(`${base}/healthz`)).status, 200);
    assert.equal((await fetch(`${base}/api/v1/attention`)).status, 401);
    const response = await fetch(`${base}/api/v1/attention`, {
      headers: { Authorization: 'Bearer abcdefghijklmnopqrstuvwxyz123456' },
    });
    assert.equal(response.status, 200);
    assert.equal((await response.json()).version, 1);
  });
});
