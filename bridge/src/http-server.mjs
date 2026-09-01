import { timingSafeEqual } from 'node:crypto';
import { createServer } from 'node:http';
import { DASHBOARD_HTML } from './dashboard.mjs';

function tokenMatches(provided, expected) {
  if (!expected) return true;
  const left = Buffer.from(provided ?? '', 'utf8');
  const right = Buffer.from(expected, 'utf8');
  return left.length === right.length && timingSafeEqual(left, right);
}

function isAuthorized(request, token) {
  if (!token) return true;
  const header = request.headers.authorization ?? '';
  const provided = header.startsWith('Bearer ') ? header.slice(7) : '';
  return tokenMatches(provided, token);
}

function sendJson(response, statusCode, body) {
  const payload = `${JSON.stringify(body)}\n`;
  response.writeHead(statusCode, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': Buffer.byteLength(payload),
    'Cache-Control': 'no-store',
    'X-Content-Type-Options': 'nosniff',
  });
  response.end(payload);
}

export function createBridgeServer({ service, token, logger = console }) {
  return createServer((request, response) => {
    void (async () => {
      const url = new URL(request.url ?? '/', 'http://bridge.local');

      if (request.method === 'GET' && url.pathname === '/') {
        response.writeHead(200, {
          'Content-Type': 'text/html; charset=utf-8',
          'Cache-Control': 'no-store',
          'X-Content-Type-Options': 'nosniff',
          'Content-Security-Policy': "default-src 'self'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'",
        });
        response.end(DASHBOARD_HTML);
        return;
      }

      if (request.method === 'GET' && url.pathname === '/healthz') {
        sendJson(response, 200, {
          ok: true,
          appServerConnected: service.connected,
          generatedAt: service.snapshot.generatedAt,
        });
        return;
      }

      if (!isAuthorized(request, token)) {
        response.setHeader('WWW-Authenticate', 'Bearer realm="Codex Attention Bridge"');
        sendJson(response, 401, { error: 'unauthorized' });
        return;
      }

      if (request.method === 'GET' && url.pathname === '/api/v1/attention') {
        sendJson(response, 200, service.snapshot);
        return;
      }

      if (request.method === 'POST' && url.pathname === '/api/v1/refresh') {
        sendJson(response, 200, await service.refresh());
        return;
      }

      sendJson(response, 404, { error: 'not_found' });
    })().catch((error) => {
      logger.error(error);
      if (!response.headersSent) sendJson(response, 500, { error: 'internal_error' });
      else response.destroy(error);
    });
  });
}

export async function listen(server, { host, port }) {
  await new Promise((resolvePromise, rejectPromise) => {
    server.once('error', rejectPromise);
    server.listen(port, host, () => {
      server.off('error', rejectPromise);
      resolvePromise();
    });
  });
}
