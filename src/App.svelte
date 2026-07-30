<script lang="ts">
  import { onMount } from 'svelte';
  import { convertFileSrc, invoke } from '@tauri-apps/api/core';
  import { listen, type UnlistenFn } from '@tauri-apps/api/event';
  import { open } from '@tauri-apps/plugin-dialog';
  import { captureClient } from './lib/capture/client';
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

  function emptySnapshot(backend = fakeBackend): CaptureSnapshot {
    return {
      revision: 0,
      phase: 'idle',
      backend,
      config: null,
      session: null,
      savedClips: 0,
      lastClip: null,
      lastError: null,
    };
  }

  let snapshot = emptySnapshot();
  let config = defaultConfig();
  let backends: BackendDescriptor[] = [fakeBackend];
  let sources: CaptureSource[] = browserSources;
  let library: ClipMetadata[] = [];
  let storage: StorageStats | null = null;
  let audioMixer: AudioMixerSnapshot | null = null;
  let doctor: DoctorReport | null = null;
  let activeView = 'capture';
  let libraryQuery = '';
  let selectedClip: ClipMetadata | null = null;
  let playbackUrl = '';
  let busy = false;
  let notice: Notice | null = null;
  let onboardingVisible = false;
  let saveTimer: ReturnType<typeof setTimeout> | undefined;
  let unlisten: (() => void) | undefined;
  let unlistenHotkey: UnlistenFn | undefined;

  $: selectedSource = sources.find((source) => source.id === config.replay.sourceId);
  $: activeAudio = config.replay.audio.systemEnabled || config.replay.audio.microphoneEnabled;
  $: currentBackend = backends.find((backend) => backend.id === snapshot.backend.id) ?? snapshot.backend;

  function showNotice(message: string, tone: Notice['tone'] = 'info') {
    notice = { message, tone };
    if (saveTimer) clearTimeout(saveTimer);
    saveTimer = setTimeout(() => (notice = null), tone === 'error' ? 7000 : 3500);
  }

  function errorText(error: unknown) {
    if (typeof error === 'object' && error !== null && 'message' in error) {
      return String((error as { message: unknown }).message);
    }
    return String(error);
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

  function applySnapshot(next: CaptureSnapshot) {
    if (next.revision < snapshot.revision) return;
    snapshot = next;
    if (next.config) config = { ...config, replay: next.config };
  }

  async function refreshAll() {
    if (!isTauri) {
      doctor = browserDoctor();
      backends = [fakeBackend];
      sources = browserSources;
      return;
    }
    [config, snapshot, backends, sources, storage] = await Promise.all([
      captureClient.getConfig(),
      captureClient.getSnapshot(),
      captureClient.listBackends(),
      captureClient.listSources(),
      captureClient.getStorageStats(),
    ]);
    if (!sources.some((source) => source.id === config.replay.sourceId)) {
      const fallback = sources.find((source) => source.isDefault) ?? sources[0];
      if (fallback) config = { ...config, replay: { ...config.replay, sourceId: fallback.id } };
    }
    await loadLibrary();
    audioMixer = await captureClient.getAudioSnapshot();
  }

  async function loadLibrary() {
    if (!isTauri) {
      library = [];
      return;
    }
    library = await captureClient.listLibrary(libraryQuery || undefined);
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

  async function persistConfig(next = config) {
    config = next;
    if (isTauri) await captureClient.saveConfig(config);
    else localStorage.setItem('moonlit-config', JSON.stringify(config));
  }

  async function updateReplay(patch: Partial<ReplayConfig>) {
    await persistConfig({ ...config, replay: { ...config.replay, ...patch } });
  }

  async function startCapture() {
    busy = true;
    try {
      if (!config.replay.sourceId) throw new Error('Selecciona una fuente de captura.');
      if (isTauri) {
        applySnapshot(await captureClient.start(config.replay));
      } else {
        applySnapshot({
          ...snapshot,
          revision: snapshot.revision + 1,
          phase: 'buffering',
          config: config.replay,
          session: {
            id: `preview-${Date.now()}`,
            sourceId: config.replay.sourceId,
            sourceLabel: selectedSource?.label ?? config.replay.sourceId,
            startedAtMs: Date.now(),
          },
          lastError: null,
        });
      }
      showNotice(snapshot.backend.simulated ? 'Buffer simulado iniciado.' : 'Buffer iniciado.', 'success');
    } catch (error) {
      showNotice(`No se pudo iniciar el buffer: ${errorText(error)}`, 'error');
    } finally {
      busy = false;
    }
  }

  async function saveClip() {
    busy = true;
    try {
      if (snapshot.phase !== 'buffering') throw new Error('Inicia el buffer antes de guardar.');
      if (isTauri) {
        applySnapshot(await captureClient.save());
        await loadLibrary();
      } else {
        applySnapshot({
          ...snapshot,
          revision: snapshot.revision + 1,
          savedClips: snapshot.savedClips + 1,
          lastClip: {
            id: `preview-${Date.now()}`,
            path: 'vista previa / manifest simulado',
            createdAtMs: Date.now(),
            durationSeconds: config.replay.bufferSeconds,
            kind: 'simulation',
            sizeBytes: 0,
            codec: config.replay.codec,
            format: config.replay.format,
            width: config.replay.resolution?.width ?? null,
            height: config.replay.resolution?.height ?? null,
            fps: config.replay.fps,
            hasAudio: activeAudio,
            proxyPath: null,
            proxyStatus: 'notNeeded',
          },
        });
      }
      showNotice('Clip guardado y añadido a la biblioteca.', 'success');
    } catch (error) {
      showNotice(`No se pudo guardar el clip: ${errorText(error)}`, 'error');
    } finally {
      busy = false;
    }
  }

  async function stopCapture() {
    busy = true;
    try {
      if (isTauri) applySnapshot(await captureClient.stop());
      else applySnapshot({ ...snapshot, revision: snapshot.revision + 1, phase: 'idle', config: null, session: null });
      showNotice('Buffer detenido.', 'info');
    } catch (error) {
      showNotice(`No se pudo detener el buffer: ${errorText(error)}`, 'error');
    } finally {
      busy = false;
    }
  }

  async function selectBackend(id: BackendId) {
    busy = true;
    try {
      if (isTauri) applySnapshot(await captureClient.selectBackend(id));
      else applySnapshot({ ...snapshot, revision: snapshot.revision + 1, backend: fakeBackend });
      config = { ...config, backend: id };
      await persistConfig(config);
      if (isTauri) sources = await captureClient.listSources();
      showNotice(`Backend ${backends.find((item) => item.id === id)?.displayName ?? id} seleccionado.`, 'success');
    } catch (error) {
      showNotice(`No se pudo cambiar el backend: ${errorText(error)}`, 'error');
    } finally {
      busy = false;
    }
  }

  async function changeStorageRoot() {
    if (!storage || !isTauri) return;
    const selected = await open({ directory: true, multiple: false, defaultPath: storage.root });
    const root = typeof selected === 'string' ? selected : null;
    if (!root || root === storage.root) return;
    try {
      storage = await captureClient.setStorageRoot(root);
      config = { ...config, storageDir: root };
      showNotice('Carpeta de clips actualizada.', 'success');
    } catch (error) {
      showNotice(`No se pudo cambiar la carpeta: ${errorText(error)}`, 'error');
    }
  }

  async function selectClip(clip: ClipMetadata) {
    selectedClip = clip;
    playbackUrl = isTauri ? convertFileSrc(clip.proxyPath ?? clip.path) : '';
    if (clip.codec === 'hevc' && clip.proxyStatus !== 'ready' && isTauri) {
      try {
        showNotice('Preparando vista previa H.264…', 'info');
        const updated = await captureClient.createClipProxy(clip.id);
        selectedClip = updated;
        playbackUrl = updated.proxyPath ? convertFileSrc(updated.proxyPath) : '';
        await loadLibrary();
        showNotice('Vista previa H.264 lista.', 'success');
      } catch (error) {
        showNotice(`No se pudo crear la vista previa H.264: ${errorText(error)}`, 'error');
      }
    }
  }

  async function updateClip(clip: ClipMetadata) {
    if (!isTauri) return;
    try {
      await captureClient.updateLibraryClip(clip.id, { title: clip.title, tags: clip.tags, favorite: clip.favorite });
      await loadLibrary();
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
      await loadLibrary();
      showNotice('Clip eliminado de la biblioteca.', 'success');
    } catch (error) {
      showNotice(`No se pudo eliminar el clip: ${errorText(error)}`, 'error');
    }
  }

  function updateSelectedClip() {
    if (selectedClip) void updateClip(selectedClip);
  }

  function deleteSelectedClip() {
    if (selectedClip) void deleteClip(selectedClip);
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

  function phaseLabel(phase: CapturePhase) {
    return { idle: 'En espera', starting: 'Iniciando', buffering: 'Buffer activo', saving: 'Guardando', stopping: 'Deteniendo', faulted: 'Error' }[phase];
  }

  onMount(() => {
    let active = true;
    const bootstrap = async () => {
      try {
        if (!isTauri) {
          const stored = localStorage.getItem('moonlit-config');
          if (stored) config = { ...defaultConfig(), ...JSON.parse(stored) };
        }
        if (isTauri) {
          unlisten = await captureClient.subscribe((event) => {
            if (!active) return;
            applySnapshot(event.snapshot);
            if (event.type === 'clipSaved') {
              void loadLibrary();
              showNotice('Clip guardado desde MoonLit.', 'success');
            }
            if (event.type === 'errorOccurred') showNotice(event.error.message, 'error');
          });
          unlistenHotkey = await listen<{ type: string }>('moonlit://hotkey', (event) => {
            if (event.payload.type === 'saveClip') void saveClip();
          });
        }
        await refreshAll();
        if (config.onboardingVersion < 1) onboardingVisible = true;
      } catch (error) {
        showNotice(`No se pudo inicializar MoonLit: ${errorText(error)}`, 'error');
      }
    };
    void bootstrap();
    return () => {
      active = false;
      unlisten?.();
      unlistenHotkey?.();
      if (saveTimer) clearTimeout(saveTimer);
    };
  });
</script>

<svelte:head><title>MoonLit | Clips locales</title></svelte:head>

<div class="app-shell">
  <aside class="sidebar">
    <div class="brand"><div class="brand-mark">M</div><div><strong>MoonLit</strong><span>clips locales</span></div></div>
    <nav aria-label="Navegación principal">
      <button class:active={activeView === 'capture'} aria-current={activeView === 'capture' ? 'page' : undefined} on:click={() => (activeView = 'capture')}><span class="nav-icon">◈</span>Captura</button>
      <button class:active={activeView === 'audio'} aria-current={activeView === 'audio' ? 'page' : undefined} on:click={() => (activeView = 'audio')}><span class="nav-icon">◒</span>Audio</button>
      <button class:active={activeView === 'library'} aria-current={activeView === 'library' ? 'page' : undefined} on:click={() => { activeView = 'library'; void loadLibrary(); }}><span class="nav-icon">▦</span>Biblioteca<span class="nav-count">{snapshot.savedClips}</span></button>
      <button class:active={activeView === 'settings'} aria-current={activeView === 'settings' ? 'page' : undefined} on:click={() => (activeView = 'settings')}><span class="nav-icon">⚙</span>Ajustes</button>
    </nav>
    <div class="sidebar-bottom"><div class="connection-dot"><span></span>{snapshot.backend.displayName}</div><small>{snapshot.backend.simulated ? 'Modo seguro de desarrollo' : 'Recorder conectado'}</small></div>
  </aside>

  <main class="main-content">
    <header class="topbar">
      <div><p class="eyebrow">CENTRO DE CONTROL</p><h1>{activeView === 'capture' ? 'Tu momento, guardado.' : activeView === 'audio' ? 'Mezcla limpia.' : activeView === 'library' ? 'Biblioteca local.' : 'Ajustes claros.'}</h1></div>
      <div class="topbar-actions"><span class:live={snapshot.phase === 'buffering'} class:error={snapshot.phase === 'faulted'} class="status-pill"><span class="status-dot"></span>{phaseLabel(snapshot.phase)}</span><button class="icon-button" aria-label="Actualizar diagnóstico" on:click={runDoctor} disabled={busy}>↻</button></div>
    </header>

    {#if activeView === 'capture'}
      <section class="hero-grid">
        <div class="hero-card"><div class="hero-copy"><p class="eyebrow accent">REPLAY BUFFER</p><h2>No vuelvas a decir<br /><em>“debí grabarlo”.</em></h2><p class="hero-description">Captura local, sin overlays ni hooks. El buffer conserva tus últimos momentos y sólo guarda cuando tú lo pides.</p></div><div class="hero-orbit orbit-one"></div><div class="hero-orbit orbit-two"></div><div class="hero-orbit orbit-three"></div><div class="hero-corner-label">LOCAL FIRST <span>01</span></div></div>
        <div class="capture-card"><div class="card-heading"><div><p class="eyebrow">CAPTURA</p><h3>Buffer de clips</h3></div><span class:recording={snapshot.phase === 'buffering'} class="recording-indicator"></span></div>
          <label for="capture-source">Fuente</label><select id="capture-source" value={config.replay.sourceId} on:change={(event) => updateReplay({ sourceId: (event.currentTarget as HTMLSelectElement).value })} disabled={busy || snapshot.phase !== 'idle'}>{#each sources as source}<option value={source.id}>{source.kind === 'window' ? 'Ventana · ' : 'Monitor · '}{source.label}</option>{/each}</select>
          <div class="two-columns"><div><label for="codec">Codec</label><select id="codec" value={config.replay.codec} on:change={(event) => updateReplay({ codec: (event.currentTarget as HTMLSelectElement).value as VideoCodec })} disabled={busy || snapshot.phase !== 'idle'}><option value="h264">H.264 / AVC</option><option value="hevc">H.265 / HEVC</option></select></div><div><label for="format">Formato</label><select id="format" value={config.replay.format} on:change={(event) => updateReplay({ format: (event.currentTarget as HTMLSelectElement).value as ContainerFormat })} disabled={busy || snapshot.phase !== 'idle'}><option value="mp4">MP4</option><option value="mkv">MKV</option></select></div></div>
          <label for="quality">Calidad</label><select id="quality" value={config.replay.quality} on:change={(event) => updateReplay({ quality: (event.currentTarget as HTMLSelectElement).value as QualityPreset })} disabled={busy || snapshot.phase !== 'idle'}><option value="low">Low · 720p30</option><option value="medium">Medium · 1080p30</option><option value="high">High · 1080p60</option><option value="ultra">Ultra · máximo disponible</option><option value="custom">Personalizada</option></select>
          <label for="buffer-length">Duración del clip</label><div class="duration-control"><input id="buffer-length" type="range" min="10" max="300" step="10" value={config.replay.bufferSeconds} on:input={(event) => updateReplay({ bufferSeconds: Number((event.currentTarget as HTMLInputElement).value) })} disabled={snapshot.phase !== 'idle' || busy} /><strong>{config.replay.bufferSeconds}<small>s</small></strong></div><div class="range-labels"><span>10 s</span><span>5 min</span></div>
          <div class="capture-actions">{#if snapshot.phase === 'buffering'}<button class="primary-button" on:click={saveClip} disabled={busy}><span>●</span> Guardar clip</button><button class="secondary-button" on:click={stopCapture} disabled={busy}>Detener</button>{:else if snapshot.phase === 'faulted'}<button class="primary-button" on:click={stopCapture} disabled={busy}>Restablecer</button><button class="secondary-button" on:click={runDoctor} disabled={busy}>Diagnóstico</button>{:else}<button class="primary-button" on:click={startCapture} disabled={busy || !selectedSource || !snapshot.backend.available}><span>▶</span> Iniciar buffer</button><button class="secondary-button" on:click={runDoctor} disabled={busy}>Diagnóstico</button>{/if}</div>
          <p class="card-footnote">{snapshot.backend.simulated ? 'Simulación activa: puedes probar todos los controles sin GPU.' : snapshot.backend.note ?? 'Backend de captura seleccionado.'}</p>
        </div>
      </section>
      <section class="stats-grid"><article class="stat-card"><span class="stat-label">FUENTE ACTUAL</span><strong>{selectedSource?.label ?? 'Ninguna seleccionada'}</strong><span class="stat-detail">{snapshot.session ? `Desde ${formatTime(snapshot.session.startedAtMs)}` : `${sources.length} fuentes disponibles`}</span></article><article class="stat-card highlight-stat"><span class="stat-label">CLIPS GUARDADOS</span><strong>{snapshot.savedClips}</strong><span class="stat-detail">Persistidos en la biblioteca</span></article><article class="stat-card"><span class="stat-label">PERFIL</span><strong>{config.replay.codec === 'hevc' ? 'H.265' : 'H.264'} · {config.replay.format.toUpperCase()}</strong><span class="stat-detail">{activeAudio ? 'Audio activado' : 'Video solamente'}</span></article></section>
      <section class="lower-grid"><article class="panel system-panel"><div class="panel-heading"><div><p class="eyebrow">COMPATIBILIDAD</p><h3>Estado del sistema</h3></div><button class="text-button" on:click={runDoctor} disabled={busy}>Volver a probar</button></div>{#if doctor}<div class="system-summary"><div class="system-main"><span class="system-icon">⌁</span><div><strong>{doctor.osName}</strong><span>{doctor.desktop} · {doctor.session}</span></div></div><span class="ready-badge">{backends.filter((item) => item.available).length} disponibles</span></div><div class="capability-list">{#each backends as backend}<div><span class:ok={backend.available} class="cap-dot"></span><span>{backend.displayName}</span><strong>{backend.available ? 'Disponible' : 'Pendiente'}</strong></div>{/each}</div>{:else}<div class="empty-state">Ejecutando diagnóstico...</div>{/if}</article><article class="panel last-clip-panel"><div class="panel-heading"><div><p class="eyebrow">ACTIVIDAD RECIENTE</p><h3>Último clip</h3></div><button class="text-button" on:click={() => (activeView = 'library')}>Ver biblioteca</button></div>{#if snapshot.lastClip}<button class="clip-preview clip-button" on:click={() => { const clip = library.find((item) => item.id === snapshot.lastClip?.id); if (clip) void selectClip(clip); activeView = 'library'; }}><div class="clip-art"><span>{snapshot.lastClip.codec === 'hevc' ? 'H.265' : 'H.264'}</span></div><div class="clip-info"><strong>{snapshot.lastClip.format.toUpperCase()} · {snapshot.lastClip.durationSeconds}s</strong><span>{formatTime(snapshot.lastClip.createdAtMs)}</span><small>{snapshot.lastClip.hasAudio ? 'Audio incluido' : 'Video solamente'}</small></div></button>{:else}<div class="empty-state"><span class="empty-icon">◇</span><span>Guarda tu primer clip<br />y aparecerá aquí.</span></div>{/if}</article></section>
    {:else if activeView === 'audio'}
      <section class="page-panel"><div class="section-intro"><p class="eyebrow accent">AUDIO MIXER</p><h2>Todo lo importante, sincronizado.</h2><p>Las muestras de audio permanecen dentro del recorder. Aquí sólo se configuran fuentes, ganancia y mute.</p></div><div class="audio-grid"><article class="audio-card"><div><span class="audio-symbol">◉</span><div><h3>Audio del sistema</h3><p>{snapshot.backend.capabilities.audio.systemAudio ? 'Loopback WASAPI disponible' : 'Disponible cuando el runtime esté validado'}</p></div></div><label class="switch-row"><input type="checkbox" checked={config.replay.audio.systemEnabled} on:change={(event) => updateReplay({ audio: { ...config.replay.audio, systemEnabled: (event.currentTarget as HTMLInputElement).checked } })} /><span class="switch"></span>Activar</label><label for="system-gain">Volumen <strong>{Math.round(config.replay.audio.systemGain * 100)}%</strong></label><input id="system-gain" type="range" min="0" max="2" step="0.05" value={config.replay.audio.systemGain} on:input={(event) => updateReplay({ audio: { ...config.replay.audio, systemGain: Number((event.currentTarget as HTMLInputElement).value) } })} /><button class="secondary-button" on:click={() => updateReplay({ audio: { ...config.replay.audio, systemMuted: !config.replay.audio.systemMuted } })}>{config.replay.audio.systemMuted ? 'Activar sonido' : 'Silenciar'}</button></article><article class="audio-card"><div><span class="audio-symbol">♩</span><div><h3>Micrófono</h3><p>{snapshot.backend.capabilities.audio.microphone ? 'Entrada disponible' : 'Disponible cuando el runtime esté validado'}</p></div></div><label class="switch-row"><input type="checkbox" checked={config.replay.audio.microphoneEnabled} on:change={(event) => updateReplay({ audio: { ...config.replay.audio, microphoneEnabled: (event.currentTarget as HTMLInputElement).checked } })} /><span class="switch"></span>Activar</label><label for="mic-gain">Volumen <strong>{Math.round(config.replay.audio.microphoneGain * 100)}%</strong></label><input id="mic-gain" type="range" min="0" max="2" step="0.05" value={config.replay.audio.microphoneGain} on:input={(event) => updateReplay({ audio: { ...config.replay.audio, microphoneGain: Number((event.currentTarget as HTMLInputElement).value) } })} /><button class="secondary-button" on:click={() => updateReplay({ audio: { ...config.replay.audio, microphoneMuted: !config.replay.audio.microphoneMuted } })}>{config.replay.audio.microphoneMuted ? 'Activar micrófono' : 'Silenciar'}</button></article></div><div class="info-banner"><strong>Sincronización A/V</strong><span>Objetivo de v1: menos de 50 ms de drift. El perfil actual usa {config.replay.audio.bitrateKbps} kbps AAC cuando el runtime real está activo.</span></div></section>
    {:else if activeView === 'library'}
      <section class="library-layout"><div class="library-toolbar"><div><p class="eyebrow accent">CLIP LIBRARY</p><h2>{library.length} clips locales</h2></div><div class="library-search"><input aria-label="Buscar clips" placeholder="Buscar por título o etiqueta" bind:value={libraryQuery} on:input={() => void loadLibrary()} /><button class="secondary-button" on:click={() => void loadLibrary()}>Actualizar</button></div></div>{#if library.length === 0}<div class="empty-large"><span class="empty-icon">▦</span><h2>Tu biblioteca está lista.</h2><p>Guarda un clip para indexarlo en SQLite. Los clips H.265 incompatibles tendrán un proxy H.264 para previsualización.</p><button class="primary-button" on:click={() => (activeView = 'capture')}>Ir a captura</button></div>{:else}<div class="library-grid">{#each library as clip}<button class:selected={selectedClip?.id === clip.id} class="library-card" on:click={() => void selectClip(clip)}><div class="thumb"><span>{clip.codec === 'hevc' ? 'H.265' : 'H.264'}</span><small>{clip.format.toUpperCase()}</small></div><div class="library-card-body"><strong>{clip.title}</strong><span>{clip.durationSeconds}s · {formatBytes(clip.sizeBytes)}</span><small>{formatTime(clip.createdAtMs)}</small></div></button>{/each}</div>{/if}{#if selectedClip}<aside class="clip-detail panel"><div class="panel-heading"><div><p class="eyebrow">DETALLE</p><h3>{selectedClip.title}</h3></div><button class="icon-button" aria-label="Cerrar detalle" on:click={() => { selectedClip = null; playbackUrl = ''; }}>×</button></div>{#if playbackUrl}<video controls src={playbackUrl} class="video-player"><track kind="captions" /></video>{:else}<div class="video-placeholder">La vista previa se generará si WebView2 no puede reproducir el codec original.</div>{/if}<label for="clip-title">Título</label><input id="clip-title" bind:value={selectedClip.title} /><label for="clip-tags">Etiquetas</label><input id="clip-tags" value={selectedClip.tags.join(', ')} on:change={(event) => (selectedClip = { ...selectedClip!, tags: (event.currentTarget as HTMLInputElement).value.split(',').map((tag) => tag.trim()).filter(Boolean) })} placeholder="juego, momento, torneo" /><div class="detail-actions"><button class="primary-button" on:click={updateSelectedClip}>Guardar metadata</button><button class="secondary-button" on:click={deleteSelectedClip}>Eliminar</button></div></aside>{/if}</section>
    {:else}
      <section class="page-panel settings-page"><div class="section-intro"><p class="eyebrow accent">SETTINGS</p><h2>Control local, sin sorpresas.</h2><p>Los ajustes se guardan en Rust y se comparten entre la UI, el hotkey y el recorder.</p></div><div class="settings-row"><div><p class="eyebrow">BACKEND</p><h3>Motor de captura</h3><p>Selecciona explícitamente el backend. No hay fallback silencioso desde producción a simulación.</p></div><div class="backend-choice">{#each backends as backend}<button class:chosen={snapshot.backend.id === backend.id} class="backend-button" aria-pressed={snapshot.backend.id === backend.id} on:click={() => void selectBackend(backend.id)} disabled={busy || snapshot.phase !== 'idle' || !backend.available}>{backend.displayName}</button>{/each}</div></div><div class="settings-row"><div><p class="eyebrow">ALMACENAMIENTO</p><h3>{storage?.root ?? 'Calculando...'}</h3><p>{storage ? `${formatBytes(storage.bytesUsed)} usados · ${storage.clipCount} archivos detectados` : 'La carpeta predeterminada es Videos/MoonLit.'}</p></div><button class="secondary-button" on:click={changeStorageRoot} disabled={!isTauri}>Cambiar carpeta</button></div><div class="settings-row"><div><p class="eyebrow">HOTKEY</p><h3>Guardar clip</h3><p>F8 será el atajo global predeterminado cuando el servicio Windows esté activo.</p></div><input class="hotkey-input" value={config.hotkeys.saveClip} on:change={(event) => persistConfig({ ...config, hotkeys: { saveClip: (event.currentTarget as HTMLInputElement).value || 'F8' } })} /></div><div class="settings-row"><div><p class="eyebrow">NOTIFICACIONES</p><h3>Avisos del sistema</h3><p>Los eventos de guardado y error también estarán disponibles con la ventana oculta.</p></div><label class="switch-row"><input type="checkbox" checked={config.notificationsEnabled} on:change={(event) => persistConfig({ ...config, notificationsEnabled: (event.currentTarget as HTMLInputElement).checked })} /><span class="switch"></span>Activadas</label></div><div class="settings-row"><div><p class="eyebrow">DIAGNÓSTICO</p><h3>{doctor?.gpu ?? 'GPU pendiente de detección'}</h3><p>{snapshot.lastError?.message ?? currentBackend.note ?? doctor?.notes[0] ?? 'Ejecuta el diagnóstico para conocer capacidades locales.'}</p></div><button class="secondary-button" on:click={runDoctor} disabled={busy}>Ejecutar</button></div></section>
    {/if}

    {#if notice}<div class:error={notice.tone === 'error'} class:success={notice.tone === 'success'} class="toast" role={notice.tone === 'error' ? 'alert' : 'status'}>{notice.message}</div>{/if}
  </main>
</div>

{#if onboardingVisible}<div class="modal-backdrop"><section class="onboarding modal"><p class="eyebrow accent">PRIMERA VEZ</p><h2>Tu captura empieza aquí.</h2><p>MoonLit es local-first. Puedes probar la interfaz con el backend simulado mientras se valida el recorder de Windows.</p><div class="onboarding-list"><span>01 <strong>Elige monitor o ventana</strong></span><span>02 <strong>Selecciona H.264 o H.265</strong></span><span>03 <strong>Guarda con el botón o F8</strong></span></div><button class="primary-button" on:click={() => { onboardingVisible = false; config = { ...config, onboardingVersion: 1 }; void persistConfig(config); }}>Empezar</button></section></div>{/if}
