import { invoke } from '@tauri-apps/api/core';
import { listen, type UnlistenFn } from '@tauri-apps/api/event';

import type {
  BackendDescriptor,
  BackendId,
  AppConfig,
  AudioMixerSnapshot,
  ClipMetadata,
  ClipUpdate,
  CaptureSnapshot,
  CaptureSource,
  RecorderEvent,
  ReplayConfig,
  StorageStats,
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
  getConfig: () => invoke<AppConfig>('get_app_config'),
  saveConfig: (config: AppConfig) => invoke<AppConfig>('save_app_config', { config }),
  listLibrary: (query?: string) => invoke<ClipMetadata[]>('list_library', { query: query ?? null }),
  getLibraryClip: (id: string) => invoke<ClipMetadata | null>('get_library_clip', { id }),
  updateLibraryClip: (id: string, update: ClipUpdate) =>
    invoke<void>('update_library_clip', { id, update }),
  deleteLibraryClip: (id: string) => invoke<void>('delete_library_clip', { id }),
  createClipProxy: (id: string) => invoke<ClipMetadata>('create_clip_proxy', { id }),
  getStorageStats: () => invoke<StorageStats>('get_storage_stats'),
  setStorageRoot: (root: string) => invoke<StorageStats>('set_storage_root', { root }),
  getAudioSnapshot: () => invoke<AudioMixerSnapshot>('get_audio_snapshot'),
  setAudioConfig: (config: ReplayConfig['audio']) => invoke<AudioMixerSnapshot>('set_audio_config', { config }),
  subscribe: (handler: (event: RecorderEvent) => void): Promise<UnlistenFn> =>
    listen<RecorderEvent>('moonlit://recorder', (event) => handler(event.payload)),
};
