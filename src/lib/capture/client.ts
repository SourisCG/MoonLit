import { invoke } from '@tauri-apps/api/core';
import { listen, type UnlistenFn } from '@tauri-apps/api/event';

import type {
  BackendDescriptor,
  BackendId,
  CaptureSnapshot,
  CaptureSource,
  RecorderEvent,
  ReplayConfig,
} from './types';

export const captureClient = {
  getSnapshot: () => invoke<CaptureSnapshot>('get_capture_snapshot'),
  listBackends: () => invoke<BackendDescriptor[]>('list_capture_backends'),
  listSources: () => invoke<CaptureSource[]>('list_capture_sources'),
  selectBackend: (backend: BackendId) =>
    invoke<CaptureSnapshot>('select_capture_backend', { backend }),
  start: (config: ReplayConfig) => invoke<CaptureSnapshot>('start_capture', { config }),
  save: () => invoke<CaptureSnapshot>('save_clip'),
  stop: () => invoke<CaptureSnapshot>('stop_capture'),
  subscribe: (handler: (event: RecorderEvent) => void): Promise<UnlistenFn> =>
    listen<RecorderEvent>('moonlit://recorder', (event) => handler(event.payload)),
};
