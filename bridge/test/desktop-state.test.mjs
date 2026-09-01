import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdir, mkdtemp, rm, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { DesktopStateReader, extractDesktopState } from '../src/desktop-state.mjs';

const A = '019a1111-1111-7111-8111-111111111111';
const B = '019a2222-2222-7222-8222-222222222222';
const C = '019a3333-3333-7333-8333-333333333333';

test('extracts host-scoped unread and pinned thread IDs', () => {
  const state = extractDesktopState({
    'electron-persisted-atom-state': {
      'unread-thread-ids-by-host-v1': { local: [A], remote: [B] },
    },
    'pinned-thread-ids': [C],
  });

  assert.deepEqual([...state.unreadIds].sort(), [A, B]);
  assert.deepEqual([...state.pinnedIds], [C]);
});

test('handles nested JSON strings without collecting unrelated text', () => {
  const state = extractDesktopState({
    payload: JSON.stringify({ unreadThreadIds: [A] }),
    title: 'this is ordinary text and must not become an id',
    other: ['opaque-but-not-under-a-recognized-key'],
  });

  assert.deepEqual([...state.unreadIds], [A]);
  assert.equal(state.pinnedIds.size, 0);
});

test('returns empty sets for an unrelated document', () => {
  const state = extractDesktopState({ hello: { world: ['x'] } });
  assert.equal(state.unreadIds.size, 0);
  assert.equal(state.pinnedIds.size, 0);
});


test('reader preserves last good IDs while reporting a transient parse failure', async () => {
  const root = await mkdtemp(join(tmpdir(), 'codex-state-'));
  await mkdir(root, { recursive: true });
  const statePath = join(root, '.codex-global-state.json');
  const reader = new DesktopStateReader(root);
  try {
    await writeFile(statePath, JSON.stringify({ unreadThreadIds: [A] }));
    const good = await reader.read();
    assert.equal(good.available, true);
    assert.deepEqual([...good.unreadIds], [A]);

    await new Promise((resolve) => setTimeout(resolve, 5));
    await writeFile(statePath, '{broken');
    const degraded = await reader.read();
    assert.equal(degraded.available, false);
    assert.deepEqual([...degraded.unreadIds], [A]);
    assert.match(degraded.error, /JSON/);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});
