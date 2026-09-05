import { asEpochSeconds, clampText, oneLine, projectName } from './util.mjs';

const WAITING_FLAGS = new Set(['waitingOnApproval', 'waitingOnUserInput']);
const PINNED_SECTION_ID = '01984de2-8f74-7c91-a3b2-5c5e937cf318';
const DEFAULT_ATTENTION_FILTER = 'unread+pinned';
const ATTENTION_FILTERS = new Set(['all', DEFAULT_ATTENTION_FILTER]);

export function normalizeAttentionFilter(value) {
  const candidate = String(value ?? DEFAULT_ATTENTION_FILTER).trim().toLowerCase();
  return ATTENTION_FILTERS.has(candidate) ? candidate : DEFAULT_ATTENTION_FILTER;
}

export function normalizeThreadStatus(status) {
  if (typeof status === 'string') {
    if (status === 'active' || status === 'running' || status === 'processing') return 'running';
    if (status === 'systemError' || status === 'error' || status === 'failed') return 'error';
    return 'idle';
  }

  const type = status?.type;
  const flags = new Set(Array.isArray(status?.activeFlags) ? status.activeFlags : []);
  if (flags.has('waitingOnApproval')) return 'waiting_approval';
  if (flags.has('waitingOnUserInput')) return 'waiting_input';
  if (type === 'active') return 'running';
  if (type === 'systemError') return 'error';
  return 'idle';
}

export function isPinnedThread(thread, pinnedIds = new Set()) {
  if (pinnedIds.has(thread.id)) return true;
  if (thread.isPinned === true || thread.pinned === true) return true;
  if (thread.section?.id === PINNED_SECTION_ID) return true;
  const sectionName = oneLine(thread.section?.name).toLowerCase();
  return sectionName === 'pinned' || sectionName === 'pin';
}

function attentionRank(item) {
  if (item.status === 'waiting_approval') return 0;
  if (item.status === 'waiting_input') return 1;
  if (item.newResult && item.unread) return 2;
  if (item.unread) return 3;
  if (item.pinned) return 4;
  return 5;
}

export function buildAttentionSnapshot({
  threads,
  unreadIds = new Set(),
  pinnedIds = new Set(),
  completedAtByThread = new Map(),
  nowSeconds = Math.floor(Date.now() / 1000),
  maxItems = 30,
  includeSubagents = false,
  attentionFilter = DEFAULT_ATTENTION_FILTER,
  desktopStateAvailable = true,
  sourceError = null,
  excludedIds = new Set(),
}) {
  const items = [];
  const filter = normalizeAttentionFilter(attentionFilter);

  for (const thread of threads ?? []) {
    if (!thread || typeof thread.id !== 'string') continue;
    if (excludedIds.has(thread.id)) continue;
    if (!includeSubagents && typeof thread.parentThreadId === 'string' && thread.parentThreadId.length > 0) continue;

    const status = normalizeThreadStatus(thread.status);
    const unread = unreadIds.has(thread.id) || thread.hasUnreadTurn === true;
    const pinned = isPinnedThread(thread, pinnedIds);
    const waiting = status === 'waiting_input' || status === 'waiting_approval';
    const included = filter === DEFAULT_ATTENTION_FILTER
      ? unread && pinned
      : waiting || unread || pinned;
    if (!included) continue;

    const updatedAt = asEpochSeconds(
      thread.recencyAt ?? thread.recency_at ?? thread.updatedAt ?? thread.updated_at ?? thread.createdAt,
    );
    const completedAt = completedAtByThread.get(thread.id) ?? 0;
    const newResult = unread && completedAt > 0;
    const title = clampText(thread.name || thread.preview || 'Untitled Codex thread', 96);
    const preview = clampText(thread.preview || '', 180);
    const cwd = typeof thread.cwd === 'string' ? thread.cwd : '';
    const reasons = [];
    if (status === 'waiting_approval') reasons.push('waiting_approval');
    else if (status === 'waiting_input') reasons.push('waiting_input');
    if (unread) reasons.push(newResult ? 'new_result' : 'unread');
    if (pinned) reasons.push('pinned');

    items.push({
      id: thread.id,
      title,
      preview,
      project: projectName(cwd),
      cwd,
      status,
      unread,
      pinned,
      newResult,
      updatedAt,
      ageSeconds: updatedAt > 0 ? Math.max(0, nowSeconds - updatedAt) : 0,
      reasons,
    });
  }

  items.sort((a, b) => {
    const rankDifference = attentionRank(a) - attentionRank(b);
    if (rankDifference !== 0) return rankDifference;
    return b.updatedAt - a.updatedAt || a.title.localeCompare(b.title);
  });

  const limited = items.slice(0, Math.max(1, maxItems));
  return {
    version: 1,
    attentionFilter: filter,
    generatedAt: new Date(nowSeconds * 1000).toISOString(),
    count: limited.length,
    totalCount: items.length,
    truncated: items.length > limited.length,
    items: limited,
    diagnostics: {
      desktopStateAvailable,
      sourceError,
    },
  };
}

export function notificationThreadId(message) {
  return message?.params?.threadId
    ?? message?.params?.thread?.id
    ?? message?.params?.turn?.threadId
    ?? message?.threadId
    ?? null;
}

export function isAttentionRefreshNotification(method) {
  return new Set([
    'thread/started',
    'thread/status/changed',
    'thread/archived',
    'thread/unarchived',
    'thread/closed',
    'thread/metadata/updated',
    'thread/section/updated',
    'turn/started',
    'turn/completed',
  ]).has(method);
}

export { WAITING_FLAGS };
