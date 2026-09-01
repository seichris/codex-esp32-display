import test from 'node:test';
import assert from 'node:assert/strict';
import { buildAttentionSnapshot, normalizeThreadStatus } from '../src/attention.mjs';

const NOW = 1_800_000_000;
const id = (n) => `019a0000-0000-7000-8000-${String(n).padStart(12, '0')}`;

function thread(n, overrides = {}) {
  return {
    id: id(n),
    name: `Thread ${n}`,
    preview: `Work item ${n}`,
    cwd: `/Users/chris/project-${n}`,
    updatedAt: NOW - n * 10,
    status: { type: 'idle' },
    ...overrides,
  };
}

test('normalizes current App Server active flags', () => {
  assert.equal(normalizeThreadStatus({ type: 'active', activeFlags: ['waitingOnApproval'] }), 'waiting_approval');
  assert.equal(normalizeThreadStatus({ type: 'active', activeFlags: ['waitingOnUserInput'] }), 'waiting_input');
  assert.equal(normalizeThreadStatus({ type: 'active', activeFlags: [] }), 'running');
  assert.equal(normalizeThreadStatus({ type: 'systemError' }), 'error');
  assert.equal(normalizeThreadStatus({ type: 'notLoaded' }), 'idle');
});

test('filters to waiting, unread, or pinned and orders by attention', () => {
  const threads = [
    thread(1, { status: { type: 'active', activeFlags: [] } }), // running only: hidden
    thread(2, { section: { id: '01984de2-8f74-7c91-a3b2-5c5e937cf318', name: 'Pinned' } }),
    thread(3),
    thread(4, { status: { type: 'active', activeFlags: ['waitingOnUserInput'] } }),
    thread(5, { status: { type: 'active', activeFlags: ['waitingOnApproval'] } }),
    thread(6),
  ];
  const unreadIds = new Set([id(3), id(6)]);
  const completedAtByThread = new Map([[id(6), NOW - 3]]);

  const snapshot = buildAttentionSnapshot({
    threads,
    unreadIds,
    completedAtByThread,
    nowSeconds: NOW,
  });

  assert.deepEqual(snapshot.items.map((item) => item.id), [id(5), id(4), id(6), id(3), id(2)]);
  assert.equal(snapshot.items[2].newResult, true);
  assert.equal(snapshot.items[3].newResult, false);
  assert.deepEqual(snapshot.items[4].reasons, ['pinned']);
});

test('keeps one card when a thread has multiple reasons', () => {
  const target = thread(7, {
    status: { type: 'active', activeFlags: ['waitingOnApproval'] },
    section: { name: 'Pinned' },
  });
  const snapshot = buildAttentionSnapshot({
    threads: [target],
    unreadIds: new Set([target.id]),
    nowSeconds: NOW,
  });

  assert.equal(snapshot.count, 1);
  assert.deepEqual(snapshot.items[0].reasons, ['waiting_approval', 'unread', 'pinned']);
});

test('caps device payload while reporting total count', () => {
  const threads = Array.from({ length: 8 }, (_, index) => thread(index + 10));
  const unreadIds = new Set(threads.map((value) => value.id));
  const snapshot = buildAttentionSnapshot({ threads, unreadIds, nowSeconds: NOW, maxItems: 3 });
  assert.equal(snapshot.count, 3);
  assert.equal(snapshot.totalCount, 8);
  assert.equal(snapshot.truncated, true);
});
