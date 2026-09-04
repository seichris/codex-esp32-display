import test from 'node:test';
import assert from 'node:assert/strict';
import { chmod, mkdir, mkdtemp, rm, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import {
  CodexAppServerClient,
  extractLatestThreadText,
  readLatestRolloutText,
} from '../src/codex-app-server.mjs';

const THREAD_ID = '019a4444-4444-7444-8444-444444444444';

test('extracts the latest agent message from newest-first turns', () => {
  const turns = [
    {
      id: 'turn-new',
      items: [
        { type: 'userMessage', content: [{ type: 'text', text: 'Please finish it' }] },
        { type: 'agentMessage', text: 'The implementation is complete.\n\nAll tests pass.' },
      ],
    },
    { id: 'turn-old', items: [{ type: 'agentMessage', text: 'Older result' }] },
  ];

  assert.deepEqual(
    extractLatestThreadText(turns, { newestFirst: true }),
    { kind: 'agent', text: 'The implementation is complete.\n\nAll tests pass.' },
  );
});

test('falls back through plan, user message, and preview', () => {
  assert.deepEqual(
    extractLatestThreadText([{ items: [{ type: 'plan', text: '1. Build\n2. Test' }] }]),
    { kind: 'plan', text: '1. Build\n2. Test' },
  );
  assert.deepEqual(
    extractLatestThreadText([{ items: [{ type: 'userMessage', content: [{ type: 'text', text: 'Do the thing' }] }] }]),
    { kind: 'user', text: 'Do the thing' },
  );
  assert.deepEqual(
    extractLatestThreadText([], { preview: 'Thread preview' }),
    { kind: 'preview', text: 'Thread preview' },
  );
});

test('reads the latest useful message from a rollout file', async () => {
  const root = await mkdtemp(join(tmpdir(), 'codex-rollout-'));
  const rolloutPath = join(root, 'sessions', 'thread.jsonl');
  await mkdir(join(root, 'sessions'), { recursive: true });
  await writeFile(rolloutPath, [
    JSON.stringify({
      type: 'event_msg',
      payload: {
        type: 'item_completed',
        thread_id: THREAD_ID,
        item: { type: 'AgentMessage', content: [{ type: 'text', text: 'Older result' }] },
      },
    }),
    JSON.stringify({
      type: 'response_item',
      payload: { type: 'message', role: 'assistant', content: [{ type: 'text', text: 'Latest result' }] },
    }),
    JSON.stringify({
      type: 'event_msg',
      payload: {
        type: 'item_completed',
        thread_id: '019a5555-5555-7555-8555-555555555555',
        item: { type: 'AgentMessage', content: [{ type: 'text', text: 'Other thread' }] },
      },
    }),
  ].join('\n'));

  try {
    assert.deepEqual(
      await readLatestRolloutText(rolloutPath, {
        threadId: THREAD_ID,
        codexHome: root,
        logger: { error() {} },
      }),
      { kind: 'agent', text: 'Latest result' },
    );
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test('falls back to rollout metadata when App Server turn APIs are unavailable', async () => {
  const root = await mkdtemp(join(tmpdir(), 'codex-client-'));
  const codexHome = join(root, 'codex-home');
  const rolloutPath = join(codexHome, 'sessions', 'thread.jsonl');
  const fakeCodex = join(root, 'fake-codex.mjs');
  await mkdir(join(codexHome, 'sessions'), { recursive: true });
  await writeFile(rolloutPath, JSON.stringify({
    type: 'event_msg',
    payload: {
      type: 'item_completed',
      thread_id: THREAD_ID,
      item: { type: 'AgentMessage', content: [{ type: 'text', text: 'Rollout fallback works' }] },
    },
  }) + '\n');
  await writeFile(fakeCodex, `#!/usr/bin/env node
import readline from 'node:readline';
const threadId = '${THREAD_ID}';
const rl = readline.createInterface({ input: process.stdin });
rl.on('line', (line) => {
  const message = JSON.parse(line);
  if (message.method === 'initialize') {
    process.stdout.write(JSON.stringify({ id: message.id, result: {} }) + '\\n');
  } else if (message.method === 'thread/turns/list' || (message.method === 'thread/read' && message.params.includeTurns)) {
    process.stdout.write(JSON.stringify({ id: message.id, error: { code: -32601, message: 'paginated_threads is not supported yet' } }) + '\\n');
  } else if (message.method === 'thread/read') {
    process.stdout.write(JSON.stringify({ id: message.id, result: {
      thread: { id: threadId, path: process.env.ROLLOUT_PATH, preview: 'Preview fallback' },
    } }) + '\\n');
  }
});
`);
  await chmod(fakeCodex, 0o755);

  const client = new CodexAppServerClient({
    command: fakeCodex,
    codexHome,
    env: { ...process.env, CODEX_HOME: codexHome, ROLLOUT_PATH: rolloutPath },
    logger: { error() {} },
  });
  try {
    await client.start();
    assert.deepEqual(
      await client.readLatestThreadText(THREAD_ID, { preview: 'Preview fallback' }),
      { kind: 'agent', text: 'Rollout fallback works' },
    );
  } finally {
    await client.close();
    await rm(root, { recursive: true, force: true });
  }
});
