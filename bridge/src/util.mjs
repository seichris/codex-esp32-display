import { homedir } from 'node:os';
import { basename, resolve } from 'node:path';

export function expandHome(input) {
  if (!input) return input;
  if (input === '~') return homedir();
  if (input.startsWith('~/')) return resolve(homedir(), input.slice(2));
  return resolve(input);
}

export function projectName(cwd) {
  if (typeof cwd !== 'string' || cwd.length === 0) return 'Codex';
  const normalized = cwd.replace(/[\\/]+$/, '');
  return basename(normalized) || 'Codex';
}

export function oneLine(value, fallback = '') {
  if (typeof value !== 'string') return fallback;
  return value.replace(/\s+/g, ' ').trim();
}

export function clampText(value, maxLength) {
  const text = oneLine(value);
  if (text.length <= maxLength) return text;
  return `${text.slice(0, Math.max(0, maxLength - 1)).trimEnd()}…`;
}

export function asEpochSeconds(value) {
  if (typeof value !== 'number' || !Number.isFinite(value)) return 0;
  // App Server timestamps are seconds. This also tolerates millisecond fixtures.
  return value > 10_000_000_000 ? Math.floor(value / 1000) : Math.floor(value);
}

export function sleep(ms) {
  return new Promise((resolvePromise) => setTimeout(resolvePromise, ms));
}
