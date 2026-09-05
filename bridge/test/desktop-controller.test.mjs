import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdir, mkdtemp, readFile, readdir, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import {
  DesktopControllerClient,
  normalizeDesktopState,
  unavailableDesktopState,
  validOpaqueId,
  validRequestId,
} from '../src/desktop-controller.mjs';

const THREAD_ID = '019a4444-4444-7444-8444-444444444444';

test('normalizes Desktop state and fails closed for malformed values', () => {
  assert.deepEqual(normalizeDesktopState(null), unavailableDesktopState());
  assert.deepEqual(normalizeDesktopState({
    available: true,
    threadId: THREAD_ID,
    focusConfidence: 'confirmed',
    voiceState: 'listening',
    capabilities: { desktopFocus: true, desktopVoiceHotkey: true },
  }), {
    available: true,
    threadId: THREAD_ID,
    focusConfidence: 'confirmed',
    voiceState: 'listening',
    capabilities: {
      desktopFocus: true,
      desktopVoiceHotkey: true,
      powerButtonLongPress: false,
    },
  });
  assert.equal(normalizeDesktopState({ threadId: 'bad id', voiceState: 'listening' }).threadId, null);
});

test('validates opaque IDs without requiring UUIDs', () => {
  assert.equal(validOpaqueId(THREAD_ID), true);
  assert.equal(validOpaqueId('opaque_thread-id-123456'), true);
  assert.equal(validOpaqueId('contains spaces'), false);
  assert.equal(validRequestId('boot:42.1'), true);
  assert.equal(validRequestId('../escape'), false);
});

test('controller without private IPC reports unavailable and rejects commands', async () => {
  const controller = new DesktopControllerClient({ directory: '', token: '' });
  assert.deepEqual(await controller.state(), unavailableDesktopState());
  await assert.rejects(
    controller.focus(THREAD_ID, 'request-1'),
    { code: 'desktop_control_unavailable', statusCode: 503 },
  );
});

test('exchanges a private IPC command and reuses an idempotent response', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'desktop-controller-test-'));
  const token = 'abcdefghijklmnopqrstuvwxyz123456';
  await mkdir(join(directory, 'requests'));
  await mkdir(join(directory, 'responses'));
  let received = 0;

  const responder = (async () => {
    const deadline = Date.now() + 2000;
    while (Date.now() < deadline) {
      const files = (await readdir(join(directory, 'requests')))
        .filter((file) => file.endsWith('.json'));
      if (files.length > 0) {
        const request = JSON.parse(await readFile(join(directory, 'requests', files[0]), 'utf8'));
        received += 1;
        await writeFile(join(directory, 'responses', `${request.ipcId}.json`), JSON.stringify({
          version: 1,
          ipcId: request.ipcId,
          ok: true,
          state: {
            threadId: request.threadId,
            focusConfidence: 'inferred',
            voiceState: 'muted',
            capabilities: { desktopFocus: true },
          },
        }));
        return;
      }
      await new Promise((resolve) => setTimeout(resolve, 10));
    }
    throw new Error('Timed out waiting for IPC request.');
  })();

  try {
    const controller = new DesktopControllerClient({ directory, token, timeoutMs: 1000 });
    const first = await controller.focus(THREAD_ID, 'focus-idempotent');
    await responder;
    const second = await controller.focus(THREAD_ID, 'focus-idempotent');
    assert.deepEqual(second, first);
    assert.equal(first.threadId, THREAD_ID);
    assert.equal(received, 1);
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});

test('a responding controller with no target does not establish Mac selection', () => {
  const state = normalizeDesktopState({
    available: true,
    threadId: null,
    focusConfidence: 'confirmed',
    capabilities: { desktopFocus: true },
  });
  assert.equal(state.available, true);
  assert.equal(state.threadId, null);
  assert.equal(state.focusConfidence, 'unavailable');
  assert.equal(state.voiceState, 'unknown');
  assert.equal(unavailableDesktopState().available, false);
});
