<script lang="ts">
  import { onMount } from 'svelte';
  import { convertFileSrc, invoke } from '@tauri-apps/api/core';
  import { listen, type UnlistenFn } from '@tauri-apps/api/event';
  import { open } from '@tauri-apps/plugin-dialog';
  import { captureClient } from './lib/capture/client';
  import {
    acceptsSnapshotRevision,
    effectiveSettingsForDisplay,
    isPlayableFileStatus,
    isSimulationClip,
  } from './lib/capture/frontend-state';
  import type {
    AppConfig,
    AudioMixerSnapshot,
    BackendDescriptor,
    BackendId,
    CapturePhase,
    CaptureSnapshot,
    CaptureSource,
    ClipMetadata,
    ContainerFormat,
    EffectiveReplaySettings,
    QualityPreset,
    ReplayConfig,
    StorageStats,
    VideoCodec,
  } from './lib/capture/types';

  type DoctorReport = {
    generatedAt: number;
    architecture: string;
    osName: string;
    osVersion: string | null;
    desktop: string;
    session: string;
    gpu: string | null;
    waylandDisplay: boolean;
    x11Display: boolean;
    commands: Array<{ name: string; available: boolean; detail: string | null }>;
    capabilities: string[];
    notes: string[];
  };

  type Notice = { message: string; tone: 'info' | 'error' | 'success' };
  type BootstrapState = 'loading' | 'ready' | 'failed' | 'preview';

  const isTauri = Boolean(
    typeof window !== 'undefined' &&
      (window as Window & { __TAURI_INTERNALS__?: unknown }).__TAURI_INTERNALS__,
  );

  const fakeBackend: BackendDescriptor = {
    id: 'fake',
    displayName: 'Modo simulación',
    available: true,
    simulated: true,
    capabilities: {
      sourceKinds: ['monitor', 'window'],
      maxResolution: { width: 3840, height: 2160 },
      maxFps: 144,
      encoders: [
        { id: 'auto', available: true, reason: null },
        { id: 'software', available: true, reason: 'x264/x265 simulado' },
      ],
      codecs: ['h264', 'hevc'],
      formats: ['mp4', 'mkv'],
      audio: {
        available: true,
        systemAudio: true,
        microphone: true,
        applicationAudio: false,
        note: 'Fuentes y niveles simulados para pruebas.',
      },
    },
    note: 'No genera video real; permite validar todos los flujos de la aplicación.',
  };

  // This descriptor is deliberately not FakeBackend.  A failed Tauri
  // bootstrap must not look like a browser preview or silently become fake.
  const unavailableHost: BackendDescriptor = {
    id: 'libobsSidecar',
    displayName: 'Host no disponible',
    available: false,
    simulated: false,
    capabilities: {
      sourceKinds: [],
      maxResolution: null,
      maxFps: null,
      encoders: [],
      codecs: [],
      formats: [],
      audio: {
        available: false,
        systemAudio: false,
        microphone: false,
        applicationAudio: false,
        note: 'No se pudo consultar el host.',
      },
    },
    note: 'No se pudo consultar el recorder. Reintenta la conexión.',
  };

  const browserSources: CaptureSource[] = [
    {
      id: 'fake-monitor-1',
      kind: 'monitor',
      label: 'Monitor principal',
      isDefault: true,
      width: 1920,
      height: 1080,
      processName: null,
      available: true,
    },
    {
      id: 'fake-window-1',
      kind: 'window',
      label: 'Fake Game Window',
      isDefault: false,
      width: 1280,
      height: 720,
      processName: 'fake-game.exe',
      available: true,
    },
  ];

  function defaultReplay(sourceId = 'fake-monitor-1'): ReplayConfig {
    return {
      sourceId,
      bufferSeconds: 30,
      resolution: null,
      fps: null,
      encoder: 'auto',
      codec: 'h264',
      format: 'mp4',
      quality: 'medium',
      bitrateKbps: null,
      audio: {
        systemEnabled: true,
        microphoneEnabled: false,
        systemDeviceId: null,
        microphoneDeviceId: null,
        systemGain: 1,
        microphoneGain: 1,
        systemMuted: false,
        microphoneMuted: false,
        bitrateKbps: 160,
      },
    };
  }

  function defaultConfig(): AppConfig {
    return {
      schemaVersion: 1,
      backend: 'fake',
      replay: defaultReplay(),
      storageDir: null,
      hotkeys: { saveClip: 'F8' },
      minimizeToTray: true,
      startMinimized: false,
      notificationsEnabled: true,
      onboardingVersion: 0,
    };
  }

  function emptySnapshot(backend: BackendDescriptor): CaptureSnapshot {
    return {
      revision: 0,
      phase: 'idle',
      backend,
      config: null,
      effective: null,
      canSave: false,
      session: null,
      savedClips: 0,
      lastClip: null,
      lastError: null,
    };
  }

  function isRecord(value: unknown): value is Record<string, unknown> {
    return typeof value === 'object' && value !== null;
  }

  function mergeStoredConfig(value: unknown): AppConfig {
    const defaults = defaultConfig();
    if (!isRecord(value)) return defaults;
    const stored = value as Partial<AppConfig> & {
      replay?: Partial<ReplayConfig> & { audio?: Partial<ReplayConfig['audio']> };
      hotkeys?: Partial<AppConfig['hotkeys']>;
    };
    return {
      ...defaults,
      ...stored,
      replay: {
        ...defaults.replay,
        ...stored.replay,
        audio: { ...defaults.replay.audio, ...stored.replay?.audio },
      },
      hotkeys: { ...defaults.hotkeys, ...stored.hotkeys },
    };
  }

  function browserDoctor(): DoctorReport {
    return {
      generatedAt: Math.floor(Date.now() / 1000),
      architecture: 'browser-preview',
      osName: 'Vista previa web',
      osVersion: null,
      desktop: 'Navegador',
      session: 'simulada',
      gpu: null,
      waylandDisplay: false,
      x11Display: false,
      commands: [],
      capabilities: ['fake-backend', 'ui-preview', 'h264', 'hevc', 'audio-simulation'],
      notes: ['Ejecuta la aplicación Tauri para consultar el sistema real.'],
    };
  }

  let snapshot = emptySnapshot(isTauri ? unavailableHost : fakeBackend);
  let config = defaultConfig();
  let backends: BackendDescriptor[] = isTauri ? [] : [fakeBackend];
  let sources: CaptureSource[] = isTauri ? [] : browserSources;
  let library: ClipMetadata[] = [];
  let storage: StorageStats | null = null;
  let audioMixer: AudioMixerSnapshot | null = null;
  let doctor: DoctorReport | null = null;
  let activeView = 'capture';
  let libraryQuery = '';
  let libraryPage = 1;
  let selectedClip: ClipMetadata | null = null;
  let playbackUrl = '';
  let playbackLoading = false;
  let playbackError: string | null = null;
  let libraryLoading = false;
  let libraryError: string | null = null;
  let busy = false;
  let notice: Notice | null = null;
  let onboardingVisible = false;
  let bootstrapState: BootstrapState = isTauri ? 'loading' : 'preview';
  let bootstrapError = '';
  let snapshotRevision: number | null = null;
  let noticeTimer: ReturnType<typeof setTimeout> | undefined;
  let libraryTimer: ReturnType<typeof setTimeout> | undefined;
  let libraryRequestId = 0;
  let configWriteChain: Promise<void> = Promise.resolve();
  let unlisten: UnlistenFn | undefined;
  let unlistenHotkey: UnlistenFn | undefined;

  const libraryPageSize = 12;

  $: selectedSource = sources.find((source) => source.id === config.replay.sourceId);
  $: currentBackend = backends.find((backend) => backend.id === snapshot.backend.id) ?? snapshot.backend;
  $: activeAudio = config.replay.audio.systemEnabled || config.replay.audio.microphoneEnabled;
  $: displayEffective = effectiveSettingsForDisplay(snapshot);
  $: libraryPageCount = Math.max(1, Math.ceil(library.length / libraryPageSize));
  $: visibleLibrary = library.slice((libraryPage - 1) * libraryPageSize, libraryPage * libraryPageSize);
  $: hostReady = bootstrapState === 'ready' || bootstrapState === 'preview';
  $: canEditConfig = hostReady && !busy && snapshot.phase === 'idle';
  $: configuredSourceMissing = !sources.some((source) => source.id === config.replay.sourceId);
  $: configuredBackendMismatch =
    bootstrapState === 'ready' && snapshot.backend.id !== config.backend;
  $: requestedEncoder = currentBackend.capabilities.encoders.find(
    (encoder) => encoder.id === config.replay.encoder,
  );
  $: requestedConfigurationSupported = Boolean(
    requestedEncoder?.available &&
      currentBackend.capabilities.codecs.includes(config.replay.codec) &&
      currentBackend.capabilities.formats.includes(config.replay.format),
  );
  $: canStart = Boolean(
    hostReady &&
      bootstrapState !== 'failed' &&
      !busy &&
      snapshot.phase === 'idle' &&
      snapshot.backend.available &&
      selectedSource?.available &&
      !configuredSourceMissing &&
      requestedConfigurationSupported,
  );

  function showNotice(message: string, tone: Notice['tone'] = 'info') {
    notice = { message, tone };
    if (noticeTimer) clearTimeout(noticeTimer);
    noticeTimer = setTimeout(() => (notice = null), tone === 'error' ? 7000 : 3500);
  }

  function errorText(error: unknown) {
    if (typeof error === 'object' && error !== null && 'message' in error) {
      return String((error as { message: unknown }).message);
    }
    return String(error);
  }

  function applySnapshot(next: CaptureSnapshot): boolean {
    if (!acceptsSnapshotRevision(snapshotRevision, next.revision)) return false;
    // A host response with revision zero is valid during initial bootstrap.
    if (snapshotRevision === null && isTauri && next.revision < snapshot.revision) return false;
    snapshotRevision = next.revision;
    snapshot = next;
    if (next.config) config = { ...config, replay: next.config };
    return true;
  }

  async function performLibraryLoad() {
    if (!isTauri) {
      libraryError = null;
      libraryLoading = false;
      return;
    }
    const requestId = ++libraryRequestId;
    const query = libraryQuery.trim() || undefined;
    libraryLoading = true;
    libraryError = null;
    try {
      const result = await captureClient.listLibrary(query);
      if (requestId !== libraryRequestId) return;
      library = result;
      libraryPage = 1;
    } catch (error) {
      if (requestId !== libraryRequestId) return;
      libraryError = errorText(error);
      showNotice(`No se pudo cargar la biblioteca: ${libraryError}`, 'error');
    } finally {
      if (requestId === libraryRequestId) libraryLoading = false;
    }
  }

  function loadLibrary(immediate = false): Promise<void> {
    if (!isTauri) {
      libraryError = null;
      libraryLoading = false;
      return Promise.resolve();
    }
    if (libraryTimer) clearTimeout(libraryTimer);
    if (immediate) return performLibraryLoad();
    return new Promise((resolve) => {
      libraryTimer = setTimeout(() => {
        libraryTimer = undefined;
        void performLibraryLoad().then(resolve, () => resolve());
      }, 250);
    });
  }

  async function refreshAll() {
    if (!isTauri) {
      doctor = browserDoctor();
      bootstrapState = 'preview';
      backends = [fakeBackend];
      sources = browserSources;
      try {
        const stored = localStorage.getItem('moonlit-config');
        if (stored) config = mergeStoredConfig(JSON.parse(stored));
      } catch (error) {
        showNotice(`No se pudo leer la configuración local: ${errorText(error)}`, 'error');
      }
      return;
    }

    bootstrapState = 'loading';
    bootstrapError = '';
    const [configResult, snapshotResult, backendsResult, sourcesResult, storageResult, audioResult] =
      await Promise.allSettled([
        captureClient.getConfig(),
        captureClient.getSnapshot(),
        captureClient.listBackends(),
        captureClient.listSources(),
        captureClient.getStorageStats(),
        captureClient.getAudioSnapshot(),
      ]);
    const coreFailures: string[] = [];

    if (configResult.status === 'fulfilled') config = configResult.value;
    else coreFailures.push(`configuración: ${errorText(configResult.reason)}`);

    if (snapshotResult.status === 'fulfilled') applySnapshot(snapshotResult.value);
    else coreFailures.push(`estado del recorder: ${errorText(snapshotResult.reason)}`);

    if (backendsResult.status === 'fulfilled') backends = backendsResult.value;
    else {
      backends = [];
      coreFailures.push(`backends: ${errorText(backendsResult.reason)}`);
    }

    if (sourcesResult.status === 'fulfilled') sources = sourcesResult.value;
    else {
      sources = [];
      coreFailures.push(`fuentes: ${errorText(sourcesResult.reason)}`);
    }

    if (storageResult.status === 'fulfilled') storage = storageResult.value;
    else showNotice(`No se pudo consultar el almacenamiento: ${errorText(storageResult.reason)}`, 'error');

    if (audioResult.status === 'fulfilled') audioMixer = audioResult.value;
    else showNotice(`No se pudo consultar el audio: ${errorText(audioResult.reason)}`, 'error');

    if (coreFailures.length > 0) {
      bootstrapState = 'failed';
      bootstrapError = coreFailures.join(' · ');
      showNotice(`MoonLit no pudo conectar con el host: ${bootstrapError}`, 'error');
      return;
    }

    bootstrapState = 'ready';
    await loadLibrary(true);
    if (configuredSourceMissing) {
      showNotice(
        `La fuente configurada "${config.replay.sourceId}" no esta disponible. No se seleccionara otra fuente automaticamente.`,
        'error',
      );
    }
    if (configuredBackendMismatch) {
      showNotice(
        `El host informa "${snapshot.backend.displayName}"; se conserva el backend configurado "${config.backend}".`,
        'error',
      );
    }
  }

  async function runDoctor() {
    busy = true;
    try {
      doctor = isTauri ? await invoke<DoctorReport>('run_doctor') : browserDoctor();
      if (isTauri) backends = await captureClient.listBackends();
      showNotice('Diagnóstico actualizado.', 'success');
    } catch (error) {
      showNotice(`No se pudo ejecutar el diagnóstico: ${errorText(error)}`, 'error');
    } finally {
      busy = false;
    }
  }

  async function persistConfig(next: AppConfig): Promise<AppConfig> {
    const previous = config;
    config = next;
    const write = async (): Promise<AppConfig> => {
      try {
        const saved = isTauri
          ? await captureClient.saveConfig(next)
          : (localStorage.setItem('moonlit-config', JSON.stringify(next)), next);
        if (
          isTauri &&
          (saved.backend !== next.backend || saved.replay.sourceId !== next.replay.sourceId)
        ) {
          throw new Error(
            'El host devolvio otro backend o fuente; se conserva la configuración anterior.',
          );
        }
        // Do not let an older write clobber a newer optimistic edit.
        if (config === next) config = saved;
        return saved;
      } catch (error) {
        // Roll back only when this write is still the visible edit.  A newer
        // queued edit must remain visible and will be persisted next.
        if (config === next) config = previous;
        throw error;
      }
    };
    const queued = configWriteChain.then(() => write(), () => write());
    configWriteChain = queued.then(
      () => undefined,
      () => undefined,
    );
    return queued;
  }

  async function saveConfigWithNotice(next: AppConfig) {
    try {
      await persistConfig(next);
    } catch (error) {
      showNotice(`No se pudo guardar la configuración: ${errorText(error)}`, 'error');
    }
  }

  async function updateReplay(patch: Partial<ReplayConfig>) {
    await saveConfigWithNotice({ ...config, replay: { ...config.replay, ...patch } });
  }

  async function startCapture() {
    if (!canStart) {
      if (configuredSourceMissing) {
        showNotice('La fuente configurada no esta disponible; elige una fuente reportada por el host.', 'error');
      } else if (!requestedConfigurationSupported) {
        showNotice('La combinación solicitada no esta soportada por el backend activo.', 'error');
      } else {
        showNotice('El backend o la fuente no estan disponibles.', 'error');
      }
      return;
    }
    busy = true;
    try {
      if (isTauri) {
        applySnapshot(await captureClient.start(config.replay));
      } else {
        const effective: EffectiveReplaySettings = {
          encoder: 'software',
          codec: config.replay.codec,
          format: config.replay.format,
        };
        applySnapshot({
          ...snapshot,
          revision: (snapshotRevision ?? snapshot.revision) + 1,
          phase: 'buffering',
          config: config.replay,
          effective,
          canSave: true,
          session: {
            id: `preview-${Date.now()}`,
            sourceId: config.replay.sourceId,
            sourceLabel: selectedSource?.label ?? config.replay.sourceId,
            startedAtMs: Date.now(),
          },
          lastError: null,
        });
      }
      showNotice(
        snapshot.backend.simulated
          ? 'Buffer simulado iniciado; no se esta capturando video real.'
          : 'Buffer iniciado.',
        'success',
      );
    } catch (error) {
      showNotice(`No se pudo iniciar el buffer: ${errorText(error)}`, 'error');
    } finally {
      busy = false;
    }
  }

  async function saveClip() {
    if (snapshot.phase !== 'buffering') {
      showNotice('Inicia el buffer antes de guardar.', 'error');
      return;
    }
    if (!snapshot.canSave) {
      showNotice('Esperando un keyframe decodificable antes de guardar.', 'info');
      return;
    }
    if (busy) return;
    busy = true;
    try {
      if (isTauri) {
        applySnapshot(await captureClient.save());
        await loadLibrary(true);
      } else {
        const effective = snapshot.effective ?? {
          encoder: 'software',
          codec: config.replay.codec,
          format: config.replay.format,
        };
        const simulatedClip: ClipMetadata = {
          id: `preview-${Date.now()}`,
          title: 'Clip simulado',
          path: 'vista previa / manifest simulado',
          createdAtMs: Date.now(),
          durationSeconds: config.replay.bufferSeconds,
          kind: 'simulation',
          sizeBytes: 0,
          codec: effective.codec,
          format: effective.format,
          width: config.replay.resolution?.width ?? null,
          height: config.replay.resolution?.height ?? null,
          fps: config.replay.fps,
          hasAudio: activeAudio,
          proxyPath: null,
          proxyStatus: 'notNeeded',
          tags: [],
          favorite: false,
          fileStatus: 'simulation',
        };
        applySnapshot({
          ...snapshot,
          revision: (snapshotRevision ?? snapshot.revision) + 1,
          savedClips: snapshot.savedClips + 1,
          lastClip: simulatedClip,
        });
        library = [simulatedClip, ...library];
      }
      showNotice(
        snapshot.backend.simulated
          ? 'Clip simulado guardado como manifest; no es un archivo de video reproducible.'
          : 'Clip guardado y añadido a la biblioteca.',
        'success',
      );
    } catch (error) {
      showNotice(`No se pudo guardar el clip: ${errorText(error)}`, 'error');
    } finally {
      busy = false;
    }
  }

  async function stopCapture() {
    if (busy) return;
    busy = true;
    try {
      if (isTauri) applySnapshot(await captureClient.stop());
      else {
        applySnapshot({
          ...snapshot,
          revision: (snapshotRevision ?? snapshot.revision) + 1,
          phase: 'idle',
          config: null,
          effective: null,
          canSave: false,
          session: null,
        });
      }
      showNotice('Buffer detenido.', 'info');
    } catch (error) {
      showNotice(`No se pudo detener el buffer: ${errorText(error)}`, 'error');
    } finally {
      busy = false;
    }
  }

  async function selectBackend(id: BackendId) {
    const requested = backends.find((backend) => backend.id === id);
    if (!requested) {
      showNotice(`El backend "${id}" no fue reportado por el host.`, 'error');
      return;
    }
    if (!requested.available) {
      showNotice(requested.note ?? 'El backend seleccionado no esta disponible.', 'error');
      return;
    }
    if (!isTauri && id !== 'fake') {
      showNotice('La vista previa web solo ofrece el backend simulado.', 'error');
      return;
    }
    if (busy || snapshot.phase !== 'idle') return;
    busy = true;
    try {
      let nextSnapshot: CaptureSnapshot;
      if (isTauri) nextSnapshot = await captureClient.selectBackend(id);
      else nextSnapshot = { ...snapshot, revision: (snapshotRevision ?? snapshot.revision) + 1, backend: requested };
      if (nextSnapshot.backend.id !== id) {
        throw new Error(
          `El host selecciono "${nextSnapshot.backend.id}" en lugar de "${id}"; no se reemplazara silenciosamente.`,
        );
      }
      applySnapshot(nextSnapshot);
      await persistConfig({ ...config, backend: id });
      if (isTauri) {
        try {
          sources = await captureClient.listSources();
        } catch (error) {
          sources = [];
          throw error;
        }
      }
      showNotice(
        requested.simulated
          ? `${requested.displayName} seleccionado: simulación visible, sin captura real.`
          : `${requested.displayName} seleccionado.`,
        'success',
      );
    } catch (error) {
      showNotice(`No se pudo cambiar el backend: ${errorText(error)}`, 'error');
    } finally {
      busy = false;
    }
  }

  async function changeStorageRoot() {
    if (!storage || !isTauri || busy) return;
    try {
      const selected = await open({ directory: true, multiple: false, defaultPath: storage.root });
      const root = typeof selected === 'string' ? selected : null;
      if (!root || root === storage.root) return;
      const previousStorage = storage;
      storage = await captureClient.setStorageRoot(root);
      try {
        await persistConfig({ ...config, storageDir: root });
      } catch (error) {
        storage = previousStorage;
        try {
          await captureClient.setStorageRoot(previousStorage.root);
        } catch {
          // Keep the mismatch visible instead of replacing it with a default.
        }
        throw error;
      }
      showNotice('Carpeta de clips actualizada.', 'success');
    } catch (error) {
      showNotice(`No se pudo cambiar la carpeta: ${errorText(error)}`, 'error');
    }
  }

  async function selectClip(clip: ClipMetadata) {
    selectedClip = clip;
    playbackUrl = '';
    playbackError = null;
    playbackLoading = false;

    if (isSimulationClip(clip)) {
      showNotice('Este elemento es un manifest simulado; nunca se enviara a un video.', 'info');
      return;
    }
    if (!isTauri) {
      showNotice('La vista previa web no abre archivos del host.', 'info');
      return;
    }
    if (!isPlayableFileStatus(clip.fileStatus)) {
      showNotice(`No se reproducira este clip: estado de archivo "${clip.fileStatus}".`, 'error');
      return;
    }

    try {
      if (clip.codec === 'hevc') {
        if (clip.proxyStatus === 'ready' && clip.proxyPath) {
          playbackUrl = convertFileSrc(clip.proxyPath);
          return;
        }
        playbackLoading = true;
        showNotice('Preparando vista previa H.264…', 'info');
        const updated = await captureClient.createClipProxy(clip.id);
        selectedClip = updated;
        if (isSimulationClip(updated) || !isPlayableFileStatus(updated.fileStatus)) {
          throw new Error(`El proxy devolvio un estado de archivo no reproducible: ${updated.fileStatus}`);
        }
        if (updated.proxyStatus !== 'ready' || !updated.proxyPath) {
          throw new Error('El host no devolvio un proxy H.264 listo.');
        }
        playbackUrl = convertFileSrc(updated.proxyPath);
        await loadLibrary(true);
        showNotice('Vista previa H.264 lista.', 'success');
      } else {
        playbackUrl = convertFileSrc(clip.path);
      }
    } catch (error) {
      playbackUrl = '';
      playbackError = errorText(error);
      showNotice(`No se pudo preparar la vista previa: ${playbackError}`, 'error');
    } finally {
      playbackLoading = false;
    }
  }

  function handlePlaybackError() {
    playbackUrl = '';
    playbackError = 'WebView2 no pudo decodificar este archivo.';
    showNotice(playbackError, 'error');
  }

  async function updateClip(clip: ClipMetadata) {
    if (!isTauri) return;
    try {
      await captureClient.updateLibraryClip(clip.id, {
        title: clip.title,
        tags: clip.tags,
        favorite: clip.favorite,
      });
      await loadLibrary(true);
      showNotice('Metadata actualizada.', 'success');
    } catch (error) {
      showNotice(`No se pudo actualizar el clip: ${errorText(error)}`, 'error');
    }
  }

  async function deleteClip(clip: ClipMetadata) {
    if (!isTauri || !window.confirm(`¿Eliminar ${clip.title}?`)) return;
    try {
      await captureClient.deleteLibraryClip(clip.id);
      if (selectedClip?.id === clip.id) {
        selectedClip = null;
        playbackUrl = '';
      }
      await loadLibrary(true);
      showNotice('Clip eliminado de la biblioteca.', 'success');
    } catch (error) {
      showNotice(`No se pudo eliminar el clip: ${errorText(error)}`, 'error');
    }
  }

  function updateSelectedTags(value: string) {
    if (!selectedClip) return;
    selectedClip = {
      ...selectedClip,
      tags: value
        .split(',')
        .map((tag) => tag.trim())
        .filter(Boolean),
    };
  }

  function updateSelectedTitle(value: string) {
    if (!selectedClip) return;
    selectedClip = { ...selectedClip, title: value };
  }

  function updateSelectedFavorite(value: boolean) {
    if (!selectedClip) return;
    selectedClip = { ...selectedClip, favorite: value };
  }

  function formatTime(timestampMs: number | null) {
    if (!timestampMs) return 'Nunca';
    return new Date(timestampMs).toLocaleString('es-ES', { dateStyle: 'medium', timeStyle: 'short' });
  }

  function formatBytes(bytes: number) {
    if (!bytes) return '0 B';
    const units = ['B', 'KB', 'MB', 'GB', 'TB'];
    const index = Math.min(Math.floor(Math.log(bytes) / Math.log(1024)), units.length - 1);
    return `${(bytes / 1024 ** index).toFixed(index ? 1 : 0)} ${units[index]}`;
  }

  function codecLabel(codec: VideoCodec) {
    return codec === 'hevc' ? 'H.265 / HEVC' : 'H.264 / AVC';
  }

  function formatLabel(format: ContainerFormat) {
    return format.toUpperCase();
  }

  function encoderLabel(encoder: string) {
    const normalized = encoder.trim().toLowerCase();
    const labels: Record<string, string> = {
      auto: 'Automático',
      nvenc: 'NVENC',
      amf: 'AMF',
      quicksync: 'QuickSync',
      'quick-sync': 'QuickSync',
      software: 'Software',
      x264: 'x264',
      x265: 'x265',
    };
    return labels[normalized] ?? encoder;
  }

  function phaseLabel(phase: CapturePhase) {
    return {
      idle: 'En espera',
      starting: 'Iniciando',
      buffering: 'Buffer activo',
      saving: 'Guardando',
      stopping: 'Deteniendo',
      faulted: 'Error',
    }[phase];
  }

  function fileStatusLabel(clip: ClipMetadata) {
    if (isSimulationClip(clip)) return 'Simulado';
    if (isPlayableFileStatus(clip.fileStatus)) return 'Disponible';
    return clip.fileStatus || 'Desconocido';
  }

  onMount(() => {
    let active = true;
    const bootstrap = async () => {
      if (isTauri) {
        try {
          unlisten = await captureClient.subscribe((event) => {
            if (!active) return;
            applySnapshot(event.snapshot);
            if (event.type === 'clipSaved') {
              void loadLibrary(true);
              showNotice('Clip guardado desde MoonLit.', 'success');
            }
            if (event.type === 'errorOccurred') showNotice(event.error.message, 'error');
          });
        } catch (error) {
          showNotice(`No se pudo suscribir al recorder: ${errorText(error)}`, 'error');
        }
        try {
          unlistenHotkey = await listen<{ type: string }>('moonlit://hotkey', (event) => {
            if (event.payload.type === 'saveClip') void saveClip();
          });
        } catch (error) {
          showNotice(`No se pudo registrar el hotkey: ${errorText(error)}`, 'error');
        }
      }

      await refreshAll();
      if (active && (bootstrapState === 'ready' || bootstrapState === 'preview') && config.onboardingVersion < 1) {
        onboardingVisible = true;
      }
    };
    void bootstrap();
    return () => {
      active = false;
      unlisten?.();
      unlistenHotkey?.();
      if (noticeTimer) clearTimeout(noticeTimer);
      if (libraryTimer) clearTimeout(libraryTimer);
    };
  });
</script>

<svelte:head><title>MoonLit | Clips locales</title></svelte:head>

<div class="app-shell">
  <aside class="sidebar" aria-label="MoonLit">
    <div class="brand"><div class="brand-mark">M</div><div><strong>MoonLit</strong><span>clips locales</span></div></div>
    <nav aria-label="Navegación principal">
      <button type="button" class:active={activeView === 'capture'} aria-current={activeView === 'capture' ? 'page' : undefined} on:click={() => (activeView = 'capture')}><span class="nav-icon">◈</span>Captura</button>
      <button type="button" class:active={activeView === 'audio'} aria-current={activeView === 'audio' ? 'page' : undefined} on:click={() => (activeView = 'audio')}><span class="nav-icon">◒</span>Audio</button>
      <button type="button" class:active={activeView === 'library'} aria-current={activeView === 'library' ? 'page' : undefined} on:click={() => { activeView = 'library'; void loadLibrary(); }}><span class="nav-icon">▦</span>Biblioteca<span class="nav-count">{snapshot.savedClips}</span></button>
      <button type="button" class:active={activeView === 'settings'} aria-current={activeView === 'settings' ? 'page' : undefined} on:click={() => (activeView = 'settings')}><span class="nav-icon">⚙</span>Ajustes</button>
    </nav>
    <div class="sidebar-bottom">
      <div class="connection-dot"><span></span>{snapshot.backend.displayName}</div>
      <small>{snapshot.backend.simulated ? 'Simulación visible: no genera video real' : bootstrapState === 'failed' ? 'Host no conectado' : 'Recorder conectado'}</small>
    </div>
  </aside>

  <main class="main-content" aria-busy={bootstrapState === 'loading'}>
    <header class="topbar">
      <div><p class="eyebrow">CENTRO DE CONTROL</p><h1>{activeView === 'capture' ? 'Tu momento, guardado.' : activeView === 'audio' ? 'Mezcla limpia.' : activeView === 'library' ? 'Biblioteca local.' : 'Ajustes claros.'}</h1></div>
      <div class="topbar-actions"><span class:live={snapshot.phase === 'buffering'} class:error={snapshot.phase === 'faulted' || bootstrapState === 'failed'} class="status-pill"><span class="status-dot"></span>{bootstrapState === 'loading' ? 'Conectando' : bootstrapState === 'failed' ? 'Host no disponible' : phaseLabel(snapshot.phase)}</span><button type="button" class="icon-button" aria-label="Actualizar diagnóstico" on:click={runDoctor} disabled={busy || bootstrapState === 'loading'}>↻</button></div>
    </header>

    {#if bootstrapState === 'failed'}
      <div class="info-banner bootstrap-banner error" role="alert"><strong>El host no responde.</strong><span>{bootstrapError}</span><button type="button" class="secondary-button" on:click={() => void refreshAll()} disabled={busy}>Reintentar conexión</button></div>
    {:else if bootstrapState === 'loading'}
      <div class="info-banner bootstrap-banner" role="status"><strong>Consultando al host…</strong><span>No se usaran valores de navegador mientras se carga el estado real.</span></div>
    {/if}

    {#if activeView === 'capture'}
      <section class="hero-grid">
        <div class="hero-card"><div class="hero-copy"><p class="eyebrow accent">REPLAY BUFFER</p><h2>No vuelvas a decir<br /><em>“debí grabarlo”.</em></h2><p class="hero-description">Captura local, sin overlays ni hooks. El buffer conserva tus últimos momentos y sólo guarda cuando tú lo pides.</p></div><div class="hero-orbit orbit-one"></div><div class="hero-orbit orbit-two"></div><div class="hero-orbit orbit-three"></div><div class="hero-corner-label">LOCAL FIRST <span>01</span></div></div>
        <div class="capture-card">
          <div class="card-heading"><div><p class="eyebrow">CAPTURA</p><h3>Buffer de clips</h3></div><span class:recording={snapshot.phase === 'buffering'} class="recording-indicator"></span></div>
          <label for="capture-source">Fuente</label>
          <select id="capture-source" value={config.replay.sourceId} on:change={(event) => void updateReplay({ sourceId: (event.currentTarget as HTMLSelectElement).value })} disabled={!canEditConfig}>
            {#if !sources.length}<option value="" disabled>El host no reporta fuentes</option>{/if}
            {#if configuredSourceMissing && config.replay.sourceId}<option value={config.replay.sourceId} disabled>Configurada pero no disponible · {config.replay.sourceId}</option>{/if}
            {#each sources as source}<option value={source.id} disabled={!source.available}>{source.kind === 'window' ? 'Ventana · ' : 'Monitor · '}{source.label}{source.available ? '' : ' · no disponible'}</option>{/each}
          </select>

          <div class="two-columns">
            <div>
              <label for="encoder">Encoder solicitado</label>
              <select id="encoder" value={config.replay.encoder} on:change={(event) => void updateReplay({ encoder: (event.currentTarget as HTMLSelectElement).value as ReplayConfig['encoder'] })} disabled={!canEditConfig}>
                {#if !requestedEncoder}<option value={config.replay.encoder} disabled>{encoderLabel(config.replay.encoder)} · no reportado</option>{/if}
                {#each currentBackend.capabilities.encoders as encoder}<option value={encoder.id} disabled={!encoder.available}>{encoderLabel(encoder.id)}{encoder.available ? '' : ` · ${encoder.reason ?? 'no disponible'}`}</option>{/each}
              </select>
            </div>
            <div>
              <label for="codec">Codec solicitado</label>
              <select id="codec" value={config.replay.codec} on:change={(event) => void updateReplay({ codec: (event.currentTarget as HTMLSelectElement).value as VideoCodec })} disabled={!canEditConfig}>
                {#if !currentBackend.capabilities.codecs.includes(config.replay.codec)}<option value={config.replay.codec} disabled>{codecLabel(config.replay.codec)} · no reportado</option>{/if}
                {#each currentBackend.capabilities.codecs as codec}<option value={codec}>{codecLabel(codec)}</option>{/each}
              </select>
            </div>
          </div>

          <label for="format">Contenedor solicitado</label>
          <select id="format" value={config.replay.format} on:change={(event) => void updateReplay({ format: (event.currentTarget as HTMLSelectElement).value as ContainerFormat })} disabled={!canEditConfig}>
            {#if !currentBackend.capabilities.formats.includes(config.replay.format)}<option value={config.replay.format} disabled>{formatLabel(config.replay.format)} · no reportado</option>{/if}
            {#each currentBackend.capabilities.formats as format}<option value={format}>{formatLabel(format)}</option>{/each}
          </select>

          <label for="quality">Calidad</label>
          <select id="quality" value={config.replay.quality} on:change={(event) => void updateReplay({ quality: (event.currentTarget as HTMLSelectElement).value as QualityPreset })} disabled={!canEditConfig}>
            <option value="low">Low · 720p30</option><option value="medium">Medium · 1080p30</option><option value="high">High · 1080p60</option><option value="ultra">Ultra · máximo disponible</option><option value="custom">Personalizada</option>
          </select>

          <label for="buffer-length">Duración del clip</label><div class="duration-control"><input id="buffer-length" type="range" min="10" max="300" step="10" value={config.replay.bufferSeconds} on:input={(event) => void updateReplay({ bufferSeconds: Number((event.currentTarget as HTMLInputElement).value) })} disabled={!canEditConfig} /><strong>{config.replay.bufferSeconds}<small>s</small></strong></div><div class="range-labels"><span>10 s</span><span>5 min</span></div>

          <div class="effective-state" aria-live="polite"><span class="stat-label">ESTADO EFECTIVO DEL HOST</span>{#if displayEffective}<strong>{encoderLabel(displayEffective.encoder)} · {codecLabel(displayEffective.codec)} · {formatLabel(displayEffective.format)}</strong><small>El recorder aceptó esta combinación.</small>{:else}<strong>Sin estado efectivo</strong><small>Se mostrará cuando el host inicie el buffer; lo solicitado no implica aceptación.</small>{/if}</div>

          {#if !requestedConfigurationSupported && hostReady}<p class="capability-warning" role="alert">El host no reporta soporte para esta combinación. No se aplicara un fallback automatico.</p>{/if}
          <div class="capture-actions">
            {#if snapshot.phase === 'buffering'}
              <button type="button" class="primary-button" on:click={() => void saveClip()} disabled={busy || !snapshot.canSave}><span>●</span> {snapshot.canSave ? 'Guardar clip' : 'Esperando keyframe…'}</button>
              <button type="button" class="secondary-button" on:click={() => void stopCapture()} disabled={busy}>Detener</button>
            {:else if snapshot.phase === 'faulted'}
              <button type="button" class="primary-button" on:click={() => void stopCapture()} disabled={busy}>Restablecer</button><button type="button" class="secondary-button" on:click={runDoctor} disabled={busy}>Diagnóstico</button>
            {:else}
              <button type="button" class="primary-button" on:click={() => void startCapture()} disabled={!canStart}><span>▶</span> Iniciar buffer</button><button type="button" class="secondary-button" on:click={runDoctor} disabled={busy || bootstrapState === 'loading'}>Diagnóstico</button>
            {/if}
          </div>
          {#if snapshot.phase === 'buffering' && !snapshot.canSave}<p class="keyframe-wait" role="status" aria-live="polite">Esperando un keyframe decodificable. Guardar permanecera desactivado hasta que el host lo confirme.</p>{/if}
          <p class="card-footnote">{snapshot.backend.simulated ? 'Simulación activa: puedes probar los controles, pero no se genera video real.' : snapshot.backend.note ?? 'Backend de captura seleccionado por el host.'}</p>
        </div>
      </section>

      <section class="stats-grid"><article class="stat-card"><span class="stat-label">FUENTE ACTUAL</span><strong>{selectedSource?.label ?? (configuredSourceMissing ? 'Fuente no disponible' : 'Ninguna seleccionada')}</strong><span class="stat-detail">{snapshot.session ? `Desde ${formatTime(snapshot.session.startedAtMs)}` : `${sources.length} fuentes reportadas por el host`}</span></article><article class="stat-card highlight-stat"><span class="stat-label">CLIPS GUARDADOS</span><strong>{snapshot.savedClips}</strong><span class="stat-detail">Persistidos en la biblioteca</span></article><article class="stat-card"><span class="stat-label">PERFIL EFECTIVO</span>{#if displayEffective}<strong>{codecLabel(displayEffective.codec)} · {formatLabel(displayEffective.format)}</strong><span class="stat-detail">{encoderLabel(displayEffective.encoder)} · {activeAudio ? 'Audio activado' : 'Video solamente'}</span>{:else if snapshot.lastClip}<strong>{codecLabel(snapshot.lastClip.codec)} · {formatLabel(snapshot.lastClip.format)}</strong><span class="stat-detail">Ultimo clip · encoder no reportado</span>{:else}<strong>Sin iniciar</strong><span class="stat-detail">No se mostraran valores solicitados como efectivos</span>{/if}</article></section>

      <section class="lower-grid"><article class="panel system-panel"><div class="panel-heading"><div><p class="eyebrow">COMPATIBILIDAD</p><h3>Estado del sistema</h3></div><button type="button" class="text-button" on:click={runDoctor} disabled={busy || bootstrapState === 'loading'}>Volver a probar</button></div>{#if doctor}<div class="system-summary"><div class="system-main"><span class="system-icon">⌁</span><div><strong>{doctor.osName}</strong><span>{doctor.desktop} · {doctor.session}</span></div></div><span class="ready-badge">{backends.filter((item) => item.available).length} disponibles</span></div><div class="capability-list">{#each backends as backend}<div><span class:ok={backend.available} class="cap-dot"></span><span>{backend.displayName}{backend.simulated ? ' · simulado' : ''}</span><strong>{backend.available ? 'Disponible' : 'Pendiente'}</strong></div>{/each}</div>{:else}<div class="empty-state">{bootstrapState === 'loading' ? 'Consultando el sistema real…' : 'Ejecuta un diagnóstico para ver capacidades.'}</div>{/if}</article><article class="panel last-clip-panel"><div class="panel-heading"><div><p class="eyebrow">ACTIVIDAD RECIENTE</p><h3>Último clip</h3></div><button type="button" class="text-button" on:click={() => (activeView = 'library')}>Ver biblioteca</button></div>{#if snapshot.lastClip}<button type="button" class="clip-preview clip-button" on:click={() => { const clip = library.find((item) => item.id === snapshot.lastClip?.id); if (clip) void selectClip(clip); activeView = 'library'; }}><div class="clip-art"><span>{isSimulationClip(snapshot.lastClip) ? 'SIMULADO' : codecLabel(snapshot.lastClip.codec)}</span></div><div class="clip-info"><strong>{formatLabel(snapshot.lastClip.format)} · {snapshot.lastClip.durationSeconds}s</strong><span>{formatTime(snapshot.lastClip.createdAtMs)}</span><small>{isSimulationClip(snapshot.lastClip) ? 'Manifest simulado · sin video' : snapshot.lastClip.hasAudio ? 'Audio incluido' : 'Video solamente'}</small></div></button>{:else}<div class="empty-state"><span class="empty-icon">◇</span><span>Guarda tu primer clip<br />y aparecerá aquí.</span></div>{/if}</article></section>
    {:else if activeView === 'audio'}
      <section class="page-panel"><div class="section-intro"><p class="eyebrow accent">AUDIO MIXER</p><h2>Todo lo importante, sincronizado.</h2><p>Las muestras de audio permanecen dentro del recorder. Aquí sólo se configuran fuentes, ganancia y mute.</p></div><div class="audio-grid"><article class="audio-card"><div><span class="audio-symbol">◉</span><div><h3>Audio del sistema</h3><p>{currentBackend.capabilities.audio.systemAudio ? 'Loopback WASAPI reportado por el host' : 'El host no reporta loopback disponible'}</p></div></div><label class="switch-row"><input type="checkbox" checked={config.replay.audio.systemEnabled} on:change={(event) => void updateReplay({ audio: { ...config.replay.audio, systemEnabled: (event.currentTarget as HTMLInputElement).checked } })} disabled={busy || snapshot.phase !== 'idle' || (!currentBackend.capabilities.audio.systemAudio && !config.replay.audio.systemEnabled)} /><span class="switch"></span>Activar</label><label for="system-gain">Volumen <strong>{Math.round(config.replay.audio.systemGain * 100)}%</strong></label><input id="system-gain" type="range" min="0" max="2" step="0.05" value={config.replay.audio.systemGain} on:input={(event) => void updateReplay({ audio: { ...config.replay.audio, systemGain: Number((event.currentTarget as HTMLInputElement).value) } })} disabled={!canEditConfig} /><button type="button" class="secondary-button" on:click={() => void updateReplay({ audio: { ...config.replay.audio, systemMuted: !config.replay.audio.systemMuted } })} disabled={!canEditConfig}>{config.replay.audio.systemMuted ? 'Activar sonido' : 'Silenciar'}</button></article><article class="audio-card"><div><span class="audio-symbol">♩</span><div><h3>Micrófono</h3><p>{currentBackend.capabilities.audio.microphone ? 'Entrada reportada por el host' : 'El host no reporta micrófono disponible'}</p></div></div><label class="switch-row"><input type="checkbox" checked={config.replay.audio.microphoneEnabled} on:change={(event) => void updateReplay({ audio: { ...config.replay.audio, microphoneEnabled: (event.currentTarget as HTMLInputElement).checked } })} disabled={busy || snapshot.phase !== 'idle' || (!currentBackend.capabilities.audio.microphone && !config.replay.audio.microphoneEnabled)} /><span class="switch"></span>Activar</label><label for="mic-gain">Volumen <strong>{Math.round(config.replay.audio.microphoneGain * 100)}%</strong></label><input id="mic-gain" type="range" min="0" max="2" step="0.05" value={config.replay.audio.microphoneGain} on:input={(event) => void updateReplay({ audio: { ...config.replay.audio, microphoneGain: Number((event.currentTarget as HTMLInputElement).value) } })} disabled={!canEditConfig} /><button type="button" class="secondary-button" on:click={() => void updateReplay({ audio: { ...config.replay.audio, microphoneMuted: !config.replay.audio.microphoneMuted } })} disabled={!canEditConfig}>{config.replay.audio.microphoneMuted ? 'Activar micrófono' : 'Silenciar'}</button></article></div><div class="info-banner"><strong>{activeAudio ? 'Audio solicitado' : 'Video solamente'}</strong><span>{audioMixer?.status ?? currentBackend.capabilities.audio.note ?? 'Estado de audio no reportado por el host.'}</span></div></section>
    {:else if activeView === 'library'}
      <section class="library-layout"><div class="library-toolbar"><div><p class="eyebrow accent">CLIP LIBRARY</p><h2>{library.length} clips encontrados</h2><span class="library-status" aria-live="polite">{libraryLoading ? 'Cargando…' : libraryError ? 'Error de biblioteca' : library.length ? `Página ${libraryPage} de ${libraryPageCount}` : 'Sin resultados'}</span></div><div class="library-search"><input aria-label="Buscar clips" placeholder="Buscar por título o etiqueta" bind:value={libraryQuery} on:input={() => { libraryPage = 1; void loadLibrary(); }} /><button type="button" class="secondary-button" on:click={() => void loadLibrary(true)} disabled={libraryLoading}>Actualizar</button></div></div>{#if libraryError}<div class="info-banner error" role="alert"><strong>No se pudo cargar la biblioteca.</strong><span>{libraryError}</span><button type="button" class="secondary-button" on:click={() => void loadLibrary(true)}>Reintentar</button></div>{/if}{#if libraryLoading && library.length === 0}<div class="empty-large" role="status">Cargando biblioteca…</div>{:else if library.length === 0}<div class="empty-large"><span class="empty-icon">▦</span><h2>{libraryQuery ? 'No hay coincidencias.' : 'Tu biblioteca está lista.'}</h2><p>Los archivos ausentes o inseguros permanecen visibles, pero nunca se envían a reproducción. Los clips H.265 incompatibles tendrán un proxy H.264 si el host lo crea.</p><button type="button" class="primary-button" on:click={() => (activeView = 'capture')}>Ir a captura</button></div>{:else}<div class="library-grid">{#each visibleLibrary as clip}<button type="button" class:selected={selectedClip?.id === clip.id} class="library-card" on:click={() => void selectClip(clip)} aria-label={`Abrir ${clip.title}`}><div class="thumb"><span>{isSimulationClip(clip) ? 'SIMULADO' : codecLabel(clip.codec)}</span><small>{formatLabel(clip.format)}</small></div><div class="library-card-body"><strong>{clip.title}</strong><span>{clip.durationSeconds}s · {formatBytes(clip.sizeBytes)}</span><small>{formatTime(clip.createdAtMs)} · <span class:file-unavailable={!isPlayableFileStatus(clip.fileStatus)} class="file-status">{fileStatusLabel(clip)}</span></small></div></button>{/each}</div>{/if}{#if libraryPageCount > 1}<div class="pagination" aria-label="Paginación de biblioteca"><button type="button" class="secondary-button" on:click={() => (libraryPage = Math.max(1, libraryPage - 1))} disabled={libraryPage === 1}>Anterior</button><span>Página {libraryPage} de {libraryPageCount}</span><button type="button" class="secondary-button" on:click={() => (libraryPage = Math.min(libraryPageCount, libraryPage + 1))} disabled={libraryPage === libraryPageCount}>Siguiente</button></div>{/if}{#if selectedClip}<div class="clip-detail panel" role="dialog" aria-label={`Detalle de ${selectedClip.title}`}><div class="panel-heading"><div><p class="eyebrow">DETALLE</p><h3>{selectedClip.title}</h3></div><button type="button" class="icon-button" aria-label="Cerrar detalle" on:click={() => { selectedClip = null; playbackUrl = ''; playbackError = null; }}>×</button></div>{#if playbackLoading}<div class="video-placeholder" role="status">Preparando vista previa…</div>{:else if playbackUrl}<video controls src={playbackUrl} class="video-player" on:error={handlePlaybackError}><track kind="captions" srclang="es" label="Español" /></video>{:else if isSimulationClip(selectedClip)}<div class="video-placeholder simulation-placeholder">Manifest simulado: no se cargara en un elemento video.</div>{:else if !isPlayableFileStatus(selectedClip.fileStatus)}<div class="video-placeholder">Archivo no reproducible: {selectedClip.fileStatus}.</div>{:else}<div class="video-placeholder">{playbackError ?? 'La vista previa se generara si el host crea un proxy compatible.'}</div>{/if}<label for="clip-title">Título</label><input id="clip-title" value={selectedClip.title} on:input={(event) => updateSelectedTitle((event.currentTarget as HTMLInputElement).value)} /><label for="clip-tags">Etiquetas</label><input id="clip-tags" value={selectedClip.tags.join(', ')} on:change={(event) => updateSelectedTags((event.currentTarget as HTMLInputElement).value)} /><label class="switch-row" for="clip-favorite"><input id="clip-favorite" type="checkbox" checked={selectedClip.favorite} on:change={(event) => updateSelectedFavorite((event.currentTarget as HTMLInputElement).checked)} /><span class="switch"></span>Favorito</label><div class="detail-actions"><button type="button" class="primary-button" on:click={() => selectedClip && void updateClip(selectedClip)} disabled={!isTauri}>Guardar</button><button type="button" class="secondary-button" on:click={() => selectedClip && void deleteClip(selectedClip)} disabled={!isTauri}>Eliminar</button></div></div>{/if}</section>
    {:else}
      <section class="page-panel settings-page"><div class="section-intro"><p class="eyebrow accent">SETTINGS</p><h2>Control local, sin sorpresas.</h2><p>Los ajustes se guardan en Rust y se comparten entre la UI, el hotkey y el recorder.</p></div><div class="settings-row"><div><p class="eyebrow">BACKEND</p><h3>Motor de captura</h3><p>Selecciona explícitamente el backend. No hay fallback silencioso desde producción a simulación.</p><span class="backend-meta">Configurado: {config.backend} · Host efectivo: {snapshot.backend.id}{snapshot.backend.simulated ? ' · SIMULADO' : ''}</span></div><div class="backend-choice">{#each backends as backend}<button type="button" class:chosen={snapshot.backend.id === backend.id} class="backend-button" aria-pressed={snapshot.backend.id === backend.id} title={backend.note ?? backend.displayName} on:click={() => void selectBackend(backend.id)} disabled={busy || snapshot.phase !== 'idle' || bootstrapState === 'loading' || !backend.available}>{backend.displayName}{backend.simulated ? ' · simulado' : ''}</button>{/each}</div></div>{#if configuredBackendMismatch}<div class="info-banner error"><strong>Backend no sustituido.</strong><span>El host informa {snapshot.backend.displayName}, pero la configuración conserva {config.backend} hasta una selección explícita.</span></div>{/if}<div class="settings-row"><div><p class="eyebrow">ALMACENAMIENTO</p><h3>{storage?.root ?? 'Calculando...'}</h3><p>{storage ? `${formatBytes(storage.bytesUsed)} usados · ${storage.clipCount} archivos detectados` : 'La carpeta predeterminada es Videos/MoonLit.'}</p></div><button type="button" class="secondary-button" on:click={() => void changeStorageRoot()} disabled={!isTauri || busy}>Cambiar carpeta</button></div><div class="settings-row"><div><p class="eyebrow">HOTKEY</p><h3>Guardar clip</h3><p>El hotkey se registra en el host; la UI conserva su estado aunque la ventana este oculta.</p></div><input class="hotkey-input" aria-label="Hotkey para guardar clip" value={config.hotkeys.saveClip} on:change={(event) => void saveConfigWithNotice({ ...config, hotkeys: { saveClip: (event.currentTarget as HTMLInputElement).value || 'F8' } })} /></div><div class="settings-row"><div><p class="eyebrow">BANDEJA</p><h3>Comportamiento de ventana</h3><p>El host decide el ciclo de vida de la ventana y la bandeja; estos valores son la configuración persistida.</p></div><div class="tray-options"><label class="switch-row"><input type="checkbox" checked={config.minimizeToTray} on:change={(event) => void saveConfigWithNotice({ ...config, minimizeToTray: (event.currentTarget as HTMLInputElement).checked })} /><span class="switch"></span>Minimizar a la bandeja</label><label class="switch-row"><input type="checkbox" checked={config.startMinimized} on:change={(event) => void saveConfigWithNotice({ ...config, startMinimized: (event.currentTarget as HTMLInputElement).checked })} /><span class="switch"></span>Iniciar minimizado</label></div></div><div class="settings-row"><div><p class="eyebrow">NOTIFICACIONES</p><h3>Avisos del sistema</h3><p>Los eventos de guardado y error también estarán disponibles con la ventana oculta.</p></div><label class="switch-row"><input type="checkbox" checked={config.notificationsEnabled} on:change={(event) => void saveConfigWithNotice({ ...config, notificationsEnabled: (event.currentTarget as HTMLInputElement).checked })} /><span class="switch"></span>Activadas</label></div><div class="info-banner"><strong>{snapshot.backend.simulated ? 'MODO SIMULACIÓN' : 'MODO HOST'}</strong><span>{snapshot.backend.simulated ? 'Los manifests simulados se muestran como tales y nunca se envian a un video.' : 'Las capacidades y los fallos mostrados proceden del host, sin valores de navegador.'}</span></div></section>
    {/if}

    {#if notice}<div class:error={notice.tone === 'error'} class:success={notice.tone === 'success'} class="toast" role={notice.tone === 'error' ? 'alert' : 'status'}>{notice.message}</div>{/if}
  </main>
</div>

{#if onboardingVisible}<div class="modal-backdrop"><div class="onboarding modal" role="dialog" aria-modal="true" aria-labelledby="onboarding-title"><p class="eyebrow accent">PRIMERA VEZ</p><h2 id="onboarding-title">Tu captura empieza aquí.</h2><p>MoonLit es local-first. Puedes probar la interfaz con el backend simulado mientras se valida el recorder de Windows.</p><div class="onboarding-list"><span>01 <strong>Elige monitor o ventana</strong></span><span>02 <strong>Selecciona H.264 o H.265</strong></span><span>03 <strong>Guarda con el botón o F8</strong></span></div><button type="button" class="primary-button" on:click={() => { onboardingVisible = false; void saveConfigWithNotice({ ...config, onboardingVersion: 1 }); }}>Empezar</button></div></div>{/if}
