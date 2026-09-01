import { EventEmitter } from 'node:events';
import { buildAttentionSnapshot, isAttentionRefreshNotification, notificationThreadId } from './attention.mjs';
import { CodexAppServerClient } from './codex-app-server.mjs';
import { DesktopStateReader } from './desktop-state.mjs';

const PINNED_SECTION_ID = '01984de2-8f74-7c91-a3b2-5c5e937cf318';

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

  async #ensureClient() {
    if (this.#client?.ready) return this.#client;
    if (this.#client) await this.#client.close().catch(() => {});

    const client = new CodexAppServerClient({
      command: this.#config.codexBin,
      env: { ...process.env, CODEX_HOME: this.#config.codexHome },
      logger: this.#logger,
    });

    client.on('notification', (message) => {
      if (message.method === 'turn/completed') {
        const id = notificationThreadId(message);
        if (id) this.#completedAtByThread.set(id, Math.floor(Date.now() / 1000));
      }
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
      // Current App Server exposes the built-in pinned section directly.
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
        // Older App Server versions may not support sectionId. Persisted pin IDs
        // and recent thread metadata still provide compatibility behavior.
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

      // Avoid firing a large stale-unread set at App Server all at once.
      for (let offset = 0; offset < missing.length; offset += 8) {
        const batch = missing.slice(offset, offset + 8);
        const reads = await Promise.allSettled(batch.map((id) => client.readThread(id)));
        for (let index = 0; index < reads.length; index++) {
          const id = batch[index];
          const result = reads[index];
          if (result.status === 'fulfilled' && result.value) {
            merged.set(id, result.value);
            this.#threadCache.set(id, result.value);
            this.#unresolvedUntil.delete(id);
          } else {
            // Stale unread IDs can exist in Desktop state. Retry later rather than
            // hammering App Server on every two-second refresh.
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
      // Once an observed result has actually been read, forget its completion
      // marker. A later manual "mark unread" should be UNREAD, not NEW.
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
      desktopStateAvailable: desktopState.available,
      sourceError: this.#sourceError,
    });
    this.emit('snapshot', this.#snapshot);
    return this.#snapshot;
  }
}
