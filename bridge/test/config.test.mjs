import test from 'node:test';
import assert from 'node:assert/strict';
import { chmod, mkdtemp, rm, writeFile } from 'node:fs/promises';
import { delimiter, join } from 'node:path';
import { tmpdir } from 'node:os';
import { normalizeAttentionFilter } from '../src/attention.mjs';
import { resolveCodexBin } from '../src/config.mjs';

test('resolves codex from PATH before falling back to a bare command', async () => {
  const root = await mkdtemp(join(tmpdir(), 'codex-bin-'));
  const binary = join(root, 'codex');
  await writeFile(binary, '#!/bin/sh\nexit 0\n');
  await chmod(binary, 0o755);
  try {
    assert.equal(resolveCodexBin('codex', { PATH: [root, '/missing'].join(delimiter) }, 'linux'), binary);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test('preserves an explicit codex command or path', () => {
  assert.equal(resolveCodexBin('custom-codex', { PATH: '' }, 'linux'), 'custom-codex');
});

test('defaults invalid attention filters to the unread and pinned mode', () => {
  assert.equal(normalizeAttentionFilter(), 'unread+pinned');
  assert.equal(normalizeAttentionFilter('ALL'), 'all');
  assert.equal(normalizeAttentionFilter('unknown'), 'unread+pinned');
});
