import { loadConfig } from './config.mjs';
import { createBridgeServer, listen } from './http-server.mjs';
import { CodexAttentionService } from './service.mjs';

async function main() {
  const config = await loadConfig();
  const service = new CodexAttentionService(config);
  const server = createBridgeServer({ service, token: config.token });

  await listen(server, config);
  console.log(`Codex Attention Bridge listening on http://${config.host}:${config.port}`);
  console.log(`Reading Codex Desktop state from ${service.desktopStatePath}`);
  console.log('The ESP32 endpoint is /api/v1/attention');

  await service.start();

  let shuttingDown = false;
  const shutdown = async (signal) => {
    if (shuttingDown) return;
    shuttingDown = true;
    console.log(`\n${signal}: shutting down`);
    await service.stop().catch(console.error);
    await new Promise((resolvePromise) => server.close(resolvePromise));
  };

  process.once('SIGINT', () => void shutdown('SIGINT'));
  process.once('SIGTERM', () => void shutdown('SIGTERM'));
}

main().catch((error) => {
  console.error(error instanceof Error ? error.stack : error);
  process.exitCode = 1;
});
