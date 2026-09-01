import test from 'node:test';
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { createBridgeServer } from '../src/http-server.mjs';

const THREAD_ID = '019a4444-4444-7444-8444-444444444444';

async function withServer(callback) {
  const service = {
    connected: true,
    snapshot: {
      version: 1,
      generatedAt: 'now',
      count: 1,
      totalCount: 1,
      items: [{ id: THREAD_ID }],
      diagnostics: {},
    },
    async refresh() { return this.snapshot; },
    async latestThread(threadId) {
      if (threadId !== THREAD_ID) {
        const error = new Error('missing');
        error.code = 'attention_thread_not_found';
        throw error;
      }
      return { version: 1, id: threadId, kind: 'agent', text: 'Latest result' };
    },
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

test('latest text endpoint is authenticated and rejects non-attention threads', async () => {
  await withServer(async (base) => {
    const endpoint = `${base}/api/v1/threads/${THREAD_ID}/latest`;
    assert.equal((await fetch(endpoint)).status, 401);

    const headers = { Authorization: 'Bearer abcdefghijklmnopqrstuvwxyz123456' };
    const response = await fetch(endpoint, { headers });
    assert.equal(response.status, 200);
    assert.deepEqual(await response.json(), {
      version: 1,
      id: THREAD_ID,
      kind: 'agent',
      text: 'Latest result',
    });

    const missing = await fetch(`${base}/api/v1/threads/not-in-inbox/latest`, { headers });
    assert.equal(missing.status, 404);
  });
});
