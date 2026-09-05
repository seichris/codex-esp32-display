import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import { DASHBOARD_HTML } from '../src/dashboard.mjs';

test('dashboard contains list and detail views with syntactically valid JavaScript', () => {
  assert.match(DASHBOARD_HTML, /id="list"/);
  assert.match(DASHBOARD_HTML, /id="detail"/);
  const match = DASHBOARD_HTML.match(/<script>([\s\S]*)<\/script>/);
  assert.ok(match, 'embedded dashboard script was not found');
  assert.doesNotThrow(() => new vm.Script(match[1]));
  assert.match(match[1], /window\.location\.hash/);
  assert.match(match[1], /history\.replaceState/);
  assert.match(match[1], /tokenStorageKey/);
});

test('dashboard seeds its token from the URL fragment before loading data', async () => {
  const match = DASHBOARD_HTML.match(/<script>([\s\S]*)<\/script>/);
  assert.ok(match, 'embedded dashboard script was not found');

  const selectors = ['#list', '#count', '#error', '#detail', '#detail-title', '#detail-meta',
    '#detail-text', '#detail-kind', '#detail-badges', '#back', '#token'];
  const elements = new Map(selectors.map((selector) => [selector, {
    hidden: false,
    innerHTML: '',
    textContent: '',
    scrollHeight: 0,
    scrollTop: 0,
    addEventListener() {},
    querySelectorAll() { return []; },
  }]));
  const stored = new Map();
  const replaced = [];
  let request;
  const context = {
    document: {
      title: 'Codex ESP32 Display',
      querySelector(selector) { return elements.get(selector); },
    },
    window: {
      location: { hash: '#token=fragment-token', pathname: '/', search: '' },
    },
    localStorage: {
      getItem(key) { return stored.get(key) ?? null; },
      setItem(key, value) { stored.set(key, value); },
      removeItem(key) { stored.delete(key); },
    },
    history: {
      replaceState(...args) { replaced.push(args); },
    },
    URLSearchParams,
    fetch: async (path, options) => {
      request = { path, options };
      return {
        ok: true,
        status: 200,
        async json() { return { totalCount: 0, items: [], attentionFilter: 'unread+pinned' }; },
      };
    },
    prompt() { return null; },
    setInterval() {},
  };

  vm.runInNewContext(match[1], context);
  await new Promise((resolve) => setImmediate(resolve));

  assert.equal(stored.get('codex-esp32-display-token'), 'fragment-token');
  assert.deepEqual(replaced, [[null, 'Codex ESP32 Display', '/']]);
  assert.equal(request.options.headers.Authorization, 'Bearer fragment-token');
});
