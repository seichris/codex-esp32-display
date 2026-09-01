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
});
