import { beforeEach, describe, expect, it, vi } from 'vitest';

const mocks = vi.hoisted(() => ({
  invoke: vi.fn(),
  listen: vi.fn(),
}));

vi.mock('@tauri-apps/api/core', () => ({ invoke: mocks.invoke }));
vi.mock('@tauri-apps/api/event', () => ({ listen: mocks.listen }));

import { captureClient } from './client';

describe('captureClient', () => {
  beforeEach(() => {
    mocks.invoke.mockReset();
    mocks.listen.mockReset();
  });

  it('uses the canonical command names and payloads', async () => {
    const config = {
      sourceId: 'fake-monitor-1',
      bufferSeconds: 30,
      resolution: null,
      fps: null,
      encoder: 'auto' as const,
      codec: 'h264' as const,
    };

    await captureClient.getSnapshot();
    await captureClient.listBackends();
    await captureClient.listSources();
    await captureClient.selectBackend('fake');
    await captureClient.start(config);
    await captureClient.save();
    await captureClient.stop();

    expect(mocks.invoke.mock.calls).toEqual([
      ['get_capture_snapshot'],
      ['list_capture_backends'],
      ['list_capture_sources'],
      ['select_capture_backend', { backend: 'fake' }],
      ['start_capture', { config }],
      ['save_clip'],
      ['stop_capture'],
    ]);
  });

  it('forwards recorder events and returns an unlistener', async () => {
    const unlisten = vi.fn();
    mocks.listen.mockResolvedValue(unlisten);
    const handler = vi.fn();

    const result = await captureClient.subscribe(handler);
    const callback = mocks.listen.mock.calls[0][1] as (event: { payload: unknown }) => void;
    const payload = { type: 'stateChanged', snapshot: { revision: 1 } };
    callback({ payload });

    expect(mocks.listen).toHaveBeenCalledWith('moonlit://recorder', expect.any(Function));
    expect(handler).toHaveBeenCalledWith(payload);
    expect(result).toBe(unlisten);
  });
});
