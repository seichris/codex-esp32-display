import { EventEmitter } from 'node:events';
import { buildAttentionSnapshot, isAttentionRefreshNotification, notificationThreadId } from './attention.mjs';
import { CodexAppServerClient } from './codex-app-server.mjs';
import { DesktopStateReader } from './desktop-state.mjs';

const PINNED_SECTION_ID = '01984de2-8f74-7c91-a3b2-5c5e937cf318';
const DETAIL_MAX_BYTES = 5600;
const DETAIL_CACHE_MS = 10_000;

export class AttentionThreadNotFoundError extends Error {
  constructor(threadId) {
    super(`Thread is not in the current attention inbox: ${threadId}`);
    this.name = 'AttentionThreadNotFoundError';
    this.code = 'attention_thread_not_found';
  }
}

function cleanDetailText(value) {
  if (typeof value !== 'string') return '';
  return value
    .replace(/<oai-mem-citation\b[\s\S]*?(?:<\/oai-mem-citation\s*>|$)/gi, '')
    .replace(/&lt;oai-mem-citation\b[\s\S]*?(?:&lt;\/oai-mem-citation\s*&gt;|$)/gi, '')
    .replace(/\r\n?/g, '\n')
    .replace(/[ \t]+\n/g, '\n')
    .replace(/\n{4,}/g, '\n\n\n')
    .trim();
}

function truncateUtf8(value, maxBytes = DETAIL_MAX_BYTES) {
  const source = Buffer.from(cleanDetailText(value), 'utf8');
  if (source.length <= maxBytes) return { text: source.toString('utf8'), truncated: false };

  let end = Math.max(0, maxBytes - Buffer.byteLength('\n\n…', 'utf8'));
  while (end > 0 && (source[end] & 0b1100_0000) === 0b1000_0000) end -= 1;
  return {
    text: `${source.subarray(0, end).toString('utf8').trimEnd()}\n\n…`,
    truncated: true,
  };
}

export class CodexAttentionService extends EventEmitter {
  #config;
  #logger;
  #desktop;
  #client = null;
  #refreshPromise = null;
  #timer = null;
  #refreshTimer = null;
  #stopped = false;
  #lastThreads = [];
  #threadCache = new Map();
  #detailCache = new Map();
  #unresolvedUntil = new Map();
  #completedAtByThread = new Map();
  #lastUnreadIds = null;
  #sourceError = null;
  #snapshot;

  constructor(config, { logger = console } = {}) {
    super();
    this.#config = config;
    this.#logger = logger;
    this.#desktop = new DesktopStateReader(config.codexHome);
    this.#snapshot = buildAttentionSnapshot({
      threads: [],
      attentionFilter: config.attentionFilter,
      desktopStateAvailable: false,
      sourceError: 'Starting Codex App Server…',
    });
  }

  get snapshot() { return this.#snapshot; }
  get connected() { return this.#client?.ready === true; }
  get desktopStatePath() { return this.#desktop.path; }

  async start() {
    this.#stopped = false;
    await this.refresh();
    this.#timer = setInterval(() => void this.refresh(), this.#config.pollIntervalMs);
    this.#timer.unref?.();
  }

  async stop() {
    this.#stopped = true;
    clearInterval(this.#timer);
    clearTimeout(this.#refreshTimer);
    this.#timer = null;
    this.#refreshTimer = null;
    await this.#client?.close();
    this.#client = null;
  }

  refresh() {
    if (this.#refreshPromise) return this.#refreshPromise;
    this.#refreshPromise = this.#doRefresh().finally(() => { this.#refreshPromise = null; });
    return this.#refreshPromise;
  }

  scheduleRefresh(delayMs = 80) {
    clearTimeout(this.#refreshTimer);
    this.#refreshTimer = setTimeout(() => void this.refresh(), delayMs);
    this.#refreshTimer.unref?.();
  }

  async latestThread(threadId) {
    const item = this.#snapshot.items.find((candidate) => candidate.id === threadId);
    if (!item) throw new AttentionThreadNotFoundError(threadId);

    const cached = this.#detailCache.get(threadId);
    if (cached && cached.updatedAt === item.updatedAt && cached.expiresAt > Date.now()) {
      return cached.value;
    }

    const client = await this.#ensureClient();
    const latest = await client.readLatestThreadText(threadId, { preview: item.preview });
    const output = truncateUtf8(latest.text);
    const value = {
      version: 1,
      id: item.id,
      title: item.title,
      project: item.project,
      status: item.status,
      reasons: item.reasons,
      updatedAt: item.updatedAt,
      generatedAt: new Date().toISOString(),
      kind: latest.kind,
      text: output.text || 'No text is available for this thread yet.',
      truncated: output.truncated,
    };

    this.#detailCache.set(threadId, {
      updatedAt: item.updatedAt,
      expiresAt: Date.now() + DETAIL_CACHE_MS,
      value,
    });
    return value;
  }

  async #ensureClient() {
    if (this.#client?.ready) return this.#client;
    if (this.#client) await this.#client.close().catch(() => {});

    const client = new CodexAppServerClient({
      command: this.#config.codexBin,
      env: { ...process.env, CODEX_HOME: this.#config.codexHome },
      codexHome: this.#config.codexHome,
      logger: this.#logger,
    });

    client.on('notification', (message) => {
      const id = notificationThreadId(message);
      if (message.method === 'turn/completed' && id) {
        this.#completedAtByThread.set(id, Math.floor(Date.now() / 1000));
        this.#detailCache.delete(id);
      }
      if (id && isAttentionRefreshNotification(message.method)) this.#detailCache.delete(id);
      if (isAttentionRefreshNotification(message.method)) this.scheduleRefresh();
    });
    client.on('exit', (error) => {
      if (this.#client === client) this.#client = null;
      this.#sourceError = error instanceof Error ? error.message : String(error);
      if (!this.#stopped) this.scheduleRefresh(1500);
    });

    this.#client = client;
    try {
      await client.start();
      return client;
    } catch (error) {
      if (this.#client === client) this.#client = null;
      await client.close().catch(() => {});
      throw error;
    }
  }

  async #doRefresh() {
    const desktopState = await this.#desktop.read();

    try {
      const client = await this.#ensureClient();
      const recent = await client.listAllThreads({ maxThreads: this.#config.maxThreads });
      const merged = new Map();
      for (const thread of recent) {
        merged.set(thread.id, thread);
        this.#threadCache.set(thread.id, thread);
      }

      // Pinned threads may be much older than the ordinary recent-page cap.
      try {
        const pinned = await client.listAllThreads({
          maxThreads: 500,
          sectionId: PINNED_SECTION_ID,
        });
        for (const thread of pinned) {
          merged.set(thread.id, thread);
          this.#threadCache.set(thread.id, thread);
        }
      } catch (error) {
        this.#logger.error(`[bridge] pinned-section query unavailable: ${error instanceof Error ? error.message : error}`);
      }

      const wantedIds = new Set([...desktopState.unreadIds, ...desktopState.pinnedIds]);
      const now = Date.now();
      const missing = [];
      for (const id of wantedIds) {
        if (merged.has(id)) continue;
        const cached = this.#threadCache.get(id);
        if (cached) {
          merged.set(id, cached);
          continue;
        }
        if ((this.#unresolvedUntil.get(id) ?? 0) <= now && missing.length < 100) missing.push(id);
      }

      for (let offset = 0; offset < missing.length; offset += 8) {
        const batch = missing.slice(offset, offset + 8);
        const reads = await Promise.allSettled(batch.map((id) => client.readThread(id)));
        for (let index = 0; index < reads.length; index += 1) {
          const id = batch[index];
          const result = reads[index];
          if (result.status === 'fulfilled' && result.value) {
            merged.set(id, result.value);
            this.#threadCache.set(id, result.value);
            this.#unresolvedUntil.delete(id);
          } else {
            this.#unresolvedUntil.set(id, now + 60_000);
          }
        }
      }

      this.#lastThreads = [...merged.values()];
      this.#sourceError = null;
    } catch (error) {
      this.#sourceError = error instanceof Error ? error.message : String(error);
      this.#logger.error(`[bridge] ${this.#sourceError}`);
      if (this.#client && !this.#client.ready) this.#client = null;
    }

    const nowSeconds = Math.floor(Date.now() / 1000);
    if (desktopState.available) {
      if (this.#lastUnreadIds) {
        for (const threadId of this.#lastUnreadIds) {
          if (!desktopState.unreadIds.has(threadId)) this.#completedAtByThread.delete(threadId);
        }
      }
      this.#lastUnreadIds = new Set(desktopState.unreadIds);
    }
    for (const [threadId, completedAt] of this.#completedAtByThread) {
      if (nowSeconds - completedAt > 7 * 24 * 60 * 60) this.#completedAtByThread.delete(threadId);
    }

    this.#snapshot = buildAttentionSnapshot({
      threads: this.#lastThreads,
      unreadIds: desktopState.unreadIds,
      pinnedIds: desktopState.pinnedIds,
      completedAtByThread: this.#completedAtByThread,
      nowSeconds,
      maxItems: this.#config.maxItems,
      attentionFilter: this.#config.attentionFilter,
      desktopStateAvailable: desktopState.available,
      sourceError: this.#sourceError,
    });

    const currentIds = new Set(this.#snapshot.items.map((item) => item.id));
    for (const threadId of this.#detailCache.keys()) {
      if (!currentIds.has(threadId)) this.#detailCache.delete(threadId);
    }

    this.emit('snapshot', this.#snapshot);
    return this.#snapshot;
  }
}
