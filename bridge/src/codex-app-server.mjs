import { spawn } from 'node:child_process';
import { EventEmitter } from 'node:events';
import { createInterface } from 'node:readline';

export class CodexAppServerError extends Error {
  constructor(message, details = undefined) {
    super(message);
    this.name = 'CodexAppServerError';
    this.details = details;
  }
}

export class CodexAppServerClient extends EventEmitter {
  #command;
  #args;
  #env;
  #cwd;
  #logger;
  #timeoutMs;
  #child = null;
  #nextId = 1;
  #pending = new Map();
  #ready = false;
  #closed = false;

  constructor({
    command = 'codex',
    args = ['app-server', '--listen', 'stdio://'],
    env = process.env,
    cwd = process.cwd(),
    logger = console,
    timeoutMs = 12_000,
  } = {}) {
    super();
    this.#command = command;
    this.#args = args;
    this.#env = env;
    this.#cwd = cwd;
    this.#logger = logger;
    this.#timeoutMs = timeoutMs;
  }

  get ready() { return this.#ready; }
  get pid() { return this.#child?.pid ?? null; }

  async start() {
    if (this.#child) throw new CodexAppServerError('App Server client was already started');
    this.#closed = false;

    const child = spawn(this.#command, this.#args, {
      cwd: this.#cwd,
      env: this.#env,
      stdio: ['pipe', 'pipe', 'pipe'],
    });
    this.#child = child;

    child.once('error', (error) => this.#handleExit(error, child));
    child.once('exit', (code, signal) => {
      const suffix = signal ? `signal ${signal}` : `code ${code}`;
      this.#handleExit(new CodexAppServerError(`codex app-server exited with ${suffix}`), child);
    });

    createInterface({ input: child.stdout, crlfDelay: Infinity }).on('line', (line) => {
      this.#handleLine(line);
    });

    createInterface({ input: child.stderr, crlfDelay: Infinity }).on('line', (line) => {
      if (line.trim()) this.#logger.error(`[codex app-server] ${line}`);
    });

    await this.request('initialize', {
      clientInfo: {
        name: 'codex_attention_display',
        title: 'Codex Attention Display',
        version: '0.1.0',
      },
      capabilities: {
        experimentalApi: true,
        optOutNotificationMethods: [
          'item/agentMessage/delta',
          'item/reasoning/textDelta',
          'item/reasoning/summaryTextDelta',
          'command/execution/outputDelta',
          'fileChange/outputDelta',
        ],
      },
    });
    await this.notify('initialized');
    this.#ready = true;
    this.emit('ready');
  }

  request(method, params = {}) {
    if (!this.#child?.stdin?.writable) {
      return Promise.reject(new CodexAppServerError('codex app-server is not writable'));
    }

    const id = this.#nextId++;
    const message = { method, id, params };

    return new Promise((resolvePromise, rejectPromise) => {
      const timer = setTimeout(() => {
        this.#pending.delete(String(id));
        rejectPromise(new CodexAppServerError(`Timed out waiting for ${method}`));
      }, this.#timeoutMs);
      timer.unref?.();

      this.#pending.set(String(id), {
        method,
        resolve: resolvePromise,
        reject: rejectPromise,
        timer,
      });

      this.#write(message).catch((error) => {
        clearTimeout(timer);
        this.#pending.delete(String(id));
        rejectPromise(error);
      });
    });
  }

  notify(method, params = undefined) {
    const message = params === undefined ? { method } : { method, params };
    return this.#write(message);
  }

  async listAllThreads({ maxThreads = 300, sectionId = undefined } = {}) {
    const threads = [];
    let cursor = null;

    while (threads.length < maxThreads) {
      const limit = Math.min(100, maxThreads - threads.length);
      const params = {
        limit,
        sortKey: 'updated_at',
        archived: false,
        sortDirection: 'desc',
      };
      if (cursor) params.cursor = cursor;
      if (sectionId !== undefined) params.sectionId = sectionId;

      const result = await this.request('thread/list', params);
      const page = Array.isArray(result?.data) ? result.data : [];
      threads.push(...page);
      cursor = result?.nextCursor ?? null;
      if (!cursor || page.length === 0) break;
    }

    return threads.slice(0, maxThreads);
  }

  async readThread(threadId) {
    const result = await this.request('thread/read', { threadId, includeTurns: false });
    return result?.thread ?? null;
  }

  async close() {
    this.#closed = true;
    this.#ready = false;
    const child = this.#child;
    this.#child = null;
    if (!child) return;
    child.stdin?.end();
    if (!child.killed) child.kill('SIGTERM');
  }

  async #write(message) {
    const child = this.#child;
    if (!child?.stdin?.writable) throw new CodexAppServerError('codex app-server stdin is closed');
    const line = `${JSON.stringify(message)}\n`;
    await new Promise((resolvePromise, rejectPromise) => {
      child.stdin.write(line, (error) => error ? rejectPromise(error) : resolvePromise());
    });
  }

  #handleLine(line) {
    let message;
    try {
      message = JSON.parse(line);
    } catch {
      this.#logger.error(`[codex app-server] ignored non-JSON stdout: ${line.slice(0, 300)}`);
      return;
    }

    if (Object.hasOwn(message, 'id') && (Object.hasOwn(message, 'result') || Object.hasOwn(message, 'error'))) {
      const pending = this.#pending.get(String(message.id));
      if (!pending) return;
      clearTimeout(pending.timer);
      this.#pending.delete(String(message.id));
      if (message.error) {
        pending.reject(new CodexAppServerError(
          `${pending.method} failed: ${message.error.message ?? 'unknown App Server error'}`,
          message.error,
        ));
      } else {
        pending.resolve(message.result);
      }
      return;
    }

    if (typeof message.method === 'string' && Object.hasOwn(message, 'id')) {
      // This observer never starts turns, so it should not receive approvals.
      // Still answer unexpected server requests so App Server cannot hang.
      void this.#write({
        id: message.id,
        error: { code: -32601, message: `Unsupported server request: ${message.method}` },
      });
      this.emit('serverRequest', message);
      return;
    }

    if (typeof message.method === 'string') {
      this.emit('notification', message);
    }
  }

  #handleExit(error, child) {
    if (this.#child !== child) return;
    this.#ready = false;
    this.#child = null;
    for (const pending of this.#pending.values()) {
      clearTimeout(pending.timer);
      pending.reject(error);
    }
    this.#pending.clear();
    if (!this.#closed) this.emit('exit', error);
  }
}
