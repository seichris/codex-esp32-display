import test from 'node:test';
import assert from 'node:assert/strict';
import { extractLatestThreadText } from '../src/codex-app-server.mjs';

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
