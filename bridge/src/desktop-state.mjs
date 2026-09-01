import { readFile, stat } from 'node:fs/promises';
import { join } from 'node:path';
import { expandHome } from './util.mjs';

const UNREAD_KEYS = new Set([
  'unread-thread-ids-by-host-v1',
  'unread-thread-ids',
  'unreadThreadIds',
]);

const PINNED_KEYS = new Set([
  'pinned-thread-ids-by-host-v1',
  'pinned-thread-ids',
  'pinnedThreadIds',
]);

function maybeParseJsonString(value) {
  if (typeof value !== 'string') return value;
  const trimmed = value.trim();
  if (!(trimmed.startsWith('{') || trimmed.startsWith('['))) return value;
  try { return JSON.parse(trimmed); } catch { return value; }
}

function collectStringLeaves(value, output, depth = 0) {
  if (depth > 12 || value == null) return;
  const parsed = maybeParseJsonString(value);
  if (typeof parsed === 'string') {
    // Thread IDs are currently UUIDv7, but keeping this permissive makes the
    // bridge resilient to future opaque IDs while excluding normal prose.
    if (parsed.length >= 16 && parsed.length <= 160 && !/\s/.test(parsed)) {
      output.add(parsed);
    }
    return;
  }
  if (Array.isArray(parsed)) {
    for (const item of parsed) collectStringLeaves(item, output, depth + 1);
    return;
  }
  if (typeof parsed === 'object') {
    for (const child of Object.values(parsed)) collectStringLeaves(child, output, depth + 1);
  }
}

function visitRecognizedKeys(value, unreadIds, pinnedIds, depth = 0) {
  if (depth > 16 || value == null) return;
  const parsed = maybeParseJsonString(value);
  if (Array.isArray(parsed)) {
    for (const child of parsed) visitRecognizedKeys(child, unreadIds, pinnedIds, depth + 1);
    return;
  }
  if (typeof parsed !== 'object') return;

  for (const [key, child] of Object.entries(parsed)) {
    if (UNREAD_KEYS.has(key)) collectStringLeaves(child, unreadIds);
    if (PINNED_KEYS.has(key)) collectStringLeaves(child, pinnedIds);
    visitRecognizedKeys(child, unreadIds, pinnedIds, depth + 1);
  }
}

export function extractDesktopState(document) {
  const unreadIds = new Set();
  const pinnedIds = new Set();
  visitRecognizedKeys(document, unreadIds, pinnedIds);
  return { unreadIds, pinnedIds };
}

export class DesktopStateReader {
  #path;
  #lastMtimeMs = -1;
  #cached = { unreadIds: new Set(), pinnedIds: new Set(), available: false, error: null };

  constructor(codexHome = '~/.codex') {
    this.#path = join(expandHome(codexHome), '.codex-global-state.json');
  }

  get path() { return this.#path; }

  async read() {
    try {
      const metadata = await stat(this.#path);
      if (metadata.mtimeMs === this.#lastMtimeMs) return this.#cached;

      const raw = await readFile(this.#path, 'utf8');
      const parsed = JSON.parse(raw);
      const extracted = extractDesktopState(parsed);
      this.#lastMtimeMs = metadata.mtimeMs;
      this.#cached = { ...extracted, available: true, error: null };
      return this.#cached;
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      this.#cached = {
        unreadIds: new Set(this.#cached.unreadIds),
        pinnedIds: new Set(this.#cached.pinnedIds),
        available: false,
        error: message,
      };
      return this.#cached;
    }
  }
}
