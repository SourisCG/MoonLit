<script lang="ts">
  import { onMount } from 'svelte';
  import { invoke } from '@tauri-apps/api/core';
  import { captureClient } from './lib/capture/client';
  import type {
    BackendDescriptor,
    BackendId,
    CapturePhase,
    CaptureSnapshot,
    CaptureSource,
    ReplayConfig,
  } from './lib/capture/types';

  type CommandProbe = {
    name: string;
    available: boolean;
    state: string;
    executable: string | null;
    exitCode: number | null;
    version: string | null;
    detail: string | null;
  };

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
    commands: CommandProbe[];
    capabilities: string[];
    notes: string[];
  };

  const fakeBackend: BackendDescriptor = {
    id: 'fake',
    displayName: 'Simulado',
    available: true,
    simulated: true,
    capabilities: {
      sourceKinds: ['monitor', 'window'],
      maxResolution: { width: 3840, height: 2160 },
      maxFps: 144,
      encoders: [
        { id: 'auto', available: true, reason: null },
        { id: 'software', available: true, reason: null },
      ],
    },
    note: 'Escribe manifests para probar el flujo sin hardware.',
  };

  const browserSources: CaptureSource[] = [
    { id: 'fake-monitor-1', kind: 'monitor', label: 'Fake Monitor 1', isDefault: true },
    { id: 'fake-window-1', kind: 'window', label: 'Fake Window 1', isDefault: false },
  ];

  function emptySnapshot(): CaptureSnapshot {
    return {
      revision: 0,
      phase: 'idle',
      backend: fakeBackend,
      config: null,
      session: null,
      savedClips: 0,
      lastClip: null,
      lastError: null,
    };
  }

  let snapshot = emptySnapshot();
  let doctor: DoctorReport | null = null;
  let backends: BackendDescriptor[] = [fakeBackend];
  let sources: CaptureSource[] = browserSources;
  let selectedSourceId = browserSources[0].id;
  let bufferSeconds = 30;
  let busy = false;
  let notice = '';
  let activeView = 'overview';

  const isTauri = Boolean(
    typeof window !== 'undefined' &&
      (window as Window & { __TAURI_INTERNALS__?: unknown }).__TAURI_INTERNALS__,
  );

  function browserDoctor(): DoctorReport {
    return {
      generatedAt: Math.floor(Date.now() / 1000),
      architecture: 'browser-preview',
      osName: 'Navegador',
      osVersion: null,
      desktop: 'Vista previa web',
      session: 'simulada',
      gpu: null,
      waylandDisplay: false,
      x11Display: false,
      commands: [],
      capabilities: ['fake-backend', 'ui-preview'],
      notes: ['Ejecuta la aplicación Tauri para consultar el sistema real.'],
    };
  }

  function browserSnapshot(): CaptureSnapshot {
    return emptySnapshot();
  }

  function applySnapshot(next: CaptureSnapshot) {
    if (next.revision < snapshot.revision) return;
    snapshot = next;
    if (next.config) {
      bufferSeconds = next.config.bufferSeconds;
      selectedSourceId = next.config.sourceId;
    }
    if (next.session) selectedSourceId = next.session.sourceId;
  }

  function errorText(error: unknown) {
    if (typeof error === 'object' && error !== null && 'message' in error) {
      return String((error as { message: unknown }).message);
    }
    return String(error);
  }

  function handleCommandError(error: unknown, operation: string) {
    notice = `${operation}: ${errorText(error)}`;
  }

  async function refreshSnapshot() {
    if (!isTauri) {
      applySnapshot(browserSnapshot());
      return;
    }
    applySnapshot(await captureClient.getSnapshot());
  }

  async function refreshBackends() {
    backends = isTauri
      ? await captureClient.listBackends()
      : [fakeBackend];
  }

  async function refreshSources() {
    sources = isTauri
      ? await captureClient.listSources()
      : browserSources;
    if (!sources.some((source) => source.id === selectedSourceId)) {
      selectedSourceId = sources.find((source) => source.isDefault)?.id ?? sources[0]?.id ?? '';
    }
  }

  async function runDoctor() {
    busy = true;
    notice = '';
    try {
      doctor = isTauri ? await invoke<DoctorReport>('run_doctor') : browserDoctor();
      await refreshBackends();
      notice = 'Diagnóstico actualizado.';
    } catch (error) {
      handleCommandError(error, 'No se pudo ejecutar el diagnóstico');
    } finally {
      busy = false;
    }
  }

  async function startCapture() {
    busy = true;
    notice = '';
    try {
      if (!selectedSourceId) throw new Error('Selecciona una fuente de captura');
      const config: ReplayConfig = {
        sourceId: selectedSourceId,
        bufferSeconds,
        resolution: null,
        fps: null,
        encoder: 'auto',
        codec: 'h264',
      };
      if (isTauri) {
        applySnapshot(await captureClient.start(config));
      } else {
        const source = sources.find((item) => item.id === selectedSourceId);
        applySnapshot({
          ...snapshot,
          revision: snapshot.revision + 1,
          phase: 'buffering',
          config,
          session: {
            id: `preview-${Date.now()}`,
            sourceId: selectedSourceId,
            sourceLabel: source?.label ?? selectedSourceId,
            startedAtMs: Date.now(),
          },
          lastError: null,
        });
      }
      notice = snapshot.backend.simulated ? 'Buffer simulado iniciado.' : 'Buffer iniciado.';
    } catch (error) {
      handleCommandError(error, 'No se pudo iniciar el buffer');
    } finally {
      busy = false;
    }
  }

  async function saveClip() {
    busy = true;
    notice = '';
    try {
      if (snapshot.phase !== 'buffering') throw new Error('Inicia el buffer antes de guardar');
      if (isTauri) {
        applySnapshot(await captureClient.save());
      } else {
        applySnapshot({
          ...snapshot,
          revision: snapshot.revision + 1,
          lastClip: {
            id: `preview-${Date.now()}`,
            path: 'vista previa / manifest simulado',
            createdAtMs: Date.now(),
            durationSeconds: bufferSeconds,
            kind: 'simulation',
          },
          savedClips: snapshot.savedClips + 1,
          lastError: null,
        });
      }
      notice = 'Clip guardado.';
    } catch (error) {
      handleCommandError(error, 'No se pudo guardar el clip');
    } finally {
      busy = false;
    }
  }

  async function stopCapture() {
    busy = true;
    notice = '';
    try {
      if (isTauri) {
        applySnapshot(await captureClient.stop());
      } else {
        applySnapshot({ ...snapshot, revision: snapshot.revision + 1, phase: 'idle', config: null, session: null });
      }
      notice = 'Buffer detenido.';
    } catch (error) {
      handleCommandError(error, 'No se pudo detener el buffer');
    } finally {
      busy = false;
    }
  }

  async function selectBackend(id: BackendId) {
    busy = true;
    notice = '';
    try {
      if (isTauri) applySnapshot(await captureClient.selectBackend(id));
      else applySnapshot({ ...snapshot, revision: snapshot.revision + 1, backend: fakeBackend });
      await refreshSources();
      notice = `Backend ${backends.find((item) => item.id === id)?.displayName ?? id} seleccionado.`;
    } catch (error) {
      handleCommandError(error, 'No se pudo cambiar el backend');
    } finally {
      busy = false;
    }
  }

  function formatTime(timestampMs: number | null) {
    if (!timestampMs) return 'Nunca';
    return new Date(timestampMs).toLocaleString('es-ES', {
      dateStyle: 'medium',
      timeStyle: 'short',
    });
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

  let unlisten: (() => void) | undefined;

  onMount(() => {
    let active = true;
    const bootstrap = async () => {
      try {
        if (isTauri) {
          unlisten = await captureClient.subscribe((payload) => {
            if (!active) return;
            if (payload.type === 'stateChanged' || payload.type === 'clipSaved' || payload.type === 'errorOccurred') {
              applySnapshot(payload.snapshot);
            }
          });
        }
        await refreshSnapshot();
        await refreshBackends();
        await refreshSources();
        await runDoctor();
      } catch (error) {
        handleCommandError(error, 'No se pudo inicializar MoonLit');
      }
    };
    void bootstrap();
    return () => {
      active = false;
      unlisten?.();
    };
  });
</script>

<svelte:head>
  <title>MoonLit | Clips locales</title>
</svelte:head>

<div class="app-shell">
  <aside class="sidebar">
    <div class="brand">
      <div class="brand-mark">S</div>
      <div>
        <strong>MoonLit</strong>
        <span>clips locales</span>
      </div>
    </div>

    <nav aria-label="Navegación principal">
      <button class:active={activeView === 'overview'} aria-current={activeView === 'overview' ? 'page' : undefined} on:click={() => (activeView = 'overview')}>
        <span class="nav-icon">◈</span>
        Resumen
      </button>
      <button class:active={activeView === 'library'} aria-current={activeView === 'library' ? 'page' : undefined} on:click={() => (activeView = 'library')}>
        <span class="nav-icon">▦</span>
        Biblioteca
        <span class="nav-count">{snapshot.savedClips}</span>
      </button>
      <button class:active={activeView === 'settings'} aria-current={activeView === 'settings' ? 'page' : undefined} on:click={() => (activeView = 'settings')}>
        <span class="nav-icon">⚙</span>
        Ajustes
      </button>
    </nav>

    <div class="sidebar-bottom">
      <div class="connection-dot"><span></span> {snapshot.backend.displayName}</div>
      <small>Contrato de captura v1</small>
    </div>
  </aside>

  <main class="main-content">
    <header class="topbar">
      <div>
        <p class="eyebrow">CENTRO DE CONTROL</p>
        <h1>{activeView === 'overview' ? 'Tu momento, guardado.' : activeView === 'library' ? 'Biblioteca local' : 'Ajustes del sistema'}</h1>
      </div>
      <div class="topbar-actions">
        <span class:live={snapshot.phase === 'buffering'} class:error={snapshot.phase === 'faulted'} class="status-pill">
          <span class="status-dot"></span>
          {phaseLabel(snapshot.phase)}
        </span>
        <button class="icon-button" aria-label="Actualizar diagnóstico" on:click={runDoctor} disabled={busy}>↻</button>
      </div>
    </header>

    {#if activeView === 'overview'}
      <section class="hero-grid">
        <div class="hero-card">
          <div class="hero-copy">
            <p class="eyebrow accent">REPLAY BUFFER</p>
            <h2>No vuelvas a decir<br /><em>“debí grabarlo”.</em></h2>
            <p class="hero-description">El contrato único permite probar el flujo completo con FakeBackend y conectar después captura Windows sin mover datos multimedia por IPC.</p>
          </div>
          <div class="hero-orbit orbit-one"></div>
          <div class="hero-orbit orbit-two"></div>
          <div class="hero-orbit orbit-three"></div>
          <div class="hero-corner-label">LOCAL FIRST <span>01</span></div>
        </div>

        <div class="capture-card">
          <div class="card-heading">
            <div>
              <p class="eyebrow">CAPTURA</p>
              <h3>Buffer de clips</h3>
            </div>
            <span class:recording={snapshot.phase === 'buffering'} class="recording-indicator"></span>
          </div>

          <label for="capture-source">Fuente</label>
          <select id="capture-source" bind:value={selectedSourceId} disabled={busy || snapshot.phase !== 'idle'}>
            {#each sources as source}
              <option value={source.id}>{source.label}</option>
            {/each}
          </select>

          <label for="buffer-length">Duración del clip</label>
          <div class="duration-control">
            <input id="buffer-length" type="range" min="10" max="300" step="10" bind:value={bufferSeconds} disabled={snapshot.phase !== 'idle' || busy} />
            <strong>{bufferSeconds}<small>s</small></strong>
          </div>
          <div class="range-labels"><span>10 s</span><span>5 min</span></div>

          <div class="capture-actions">
            {#if snapshot.phase === 'buffering'}
              <button class="primary-button" on:click={saveClip} disabled={busy}><span>●</span> Guardar clip</button>
              <button class="secondary-button" on:click={stopCapture} disabled={busy}>Detener</button>
            {:else}
              <button class="primary-button" on:click={startCapture} disabled={busy || !selectedSourceId || !snapshot.backend.available}><span>▶</span> Iniciar buffer</button>
              <button class="secondary-button" on:click={runDoctor} disabled={busy}>Diagnóstico</button>
            {/if}
          </div>
          <p class="card-footnote">{snapshot.backend.simulated ? 'El manifest guardado es simulado y no contiene vídeo.' : snapshot.backend.note ?? 'Backend nativo seleccionado.'}</p>
        </div>
      </section>

      <section class="stats-grid">
        <article class="stat-card">
          <span class="stat-label">FUENTE ACTUAL</span>
          <strong>{snapshot.session?.sourceLabel ?? 'Ninguna seleccionada'}</strong>
          <span class="stat-detail">{snapshot.session ? `Desde ${formatTime(snapshot.session.startedAtMs)}` : 'Esperando actividad'}</span>
        </article>
        <article class="stat-card highlight-stat">
          <span class="stat-label">CLIPS GUARDADOS</span>
          <strong>{snapshot.savedClips}</strong>
          <span class="stat-detail">En esta sesión de prueba</span>
        </article>
        <article class="stat-card">
          <span class="stat-label">BACKEND</span>
          <strong>{snapshot.backend.displayName}</strong>
          <span class="stat-detail">{snapshot.backend.available ? (snapshot.backend.simulated ? 'Modo seguro' : 'Disponible') : 'No disponible'}</span>
        </article>
      </section>

      <section class="lower-grid">
        <article class="panel system-panel">
          <div class="panel-heading">
            <div>
              <p class="eyebrow">COMPATIBILIDAD</p>
              <h3>Estado del sistema</h3>
            </div>
            <button class="text-button" on:click={runDoctor} disabled={busy}>Volver a probar</button>
          </div>
          {#if doctor}
            <div class="system-summary">
              <div class="system-main"><span class="system-icon">⌁</span><div><strong>{doctor.osName}</strong><span>{doctor.desktop} · {doctor.session}</span></div></div>
              <span class="ready-badge">{backends.filter((backend) => backend.available).length} disponibles</span>
            </div>
            <div class="capability-list">
              {#each backends as backend}
                <div><span class:ok={backend.available} class="cap-dot"></span><span>{backend.displayName}</span><strong>{backend.available ? 'Disponible' : 'Pendiente'}</strong></div>
              {/each}
            </div>
          {:else}
            <div class="empty-state">Ejecutando diagnóstico...</div>
          {/if}
        </article>

        <article class="panel last-clip-panel">
          <div class="panel-heading"><div><p class="eyebrow">ACTIVIDAD RECIENTE</p><h3>Último clip</h3></div><span class="panel-arrow">↗</span></div>
          {#if snapshot.lastClip}
            <div class="clip-preview"><div class="clip-art"><span>PLAY</span></div><div class="clip-info"><strong>{snapshot.lastClip.kind === 'simulation' ? 'Clip simulado' : 'Clip multimedia'}</strong><span>{snapshot.lastClip.durationSeconds}s · {formatTime(snapshot.lastClip.createdAtMs)}</span><small>{snapshot.lastClip.path}</small></div></div>
          {:else}
            <div class="empty-state"><span class="empty-icon">◇</span><span>Guarda tu primer clip<br />y aparecerá aquí.</span></div>
          {/if}
        </article>
      </section>
    {:else if activeView === 'library'}
      <section class="page-panel"><div class="empty-large"><span class="empty-icon">▦</span><h2>Biblioteca en construcción</h2><p>La persistencia SQLite será el siguiente módulo. El runtime ya devuelve metadata de clips mediante el contrato único.</p><button class="primary-button" on:click={() => (activeView = 'overview')}>Volver al resumen</button></div></section>
    {:else}
      <section class="page-panel settings-page">
        <div class="settings-row">
          <div><p class="eyebrow">BACKEND DE CAPTURA</p><h3>Backend actual</h3><p>La aplicación muestra únicamente backends publicados por la factory de la plataforma. No hay fallback silencioso a simulación.</p></div>
          <div class="backend-choice">
            {#each backends as backend}
              <button class:chosen={snapshot.backend.id === backend.id} class="backend-button" aria-pressed={snapshot.backend.id === backend.id} on:click={() => selectBackend(backend.id)} disabled={busy || snapshot.phase !== 'idle' || !backend.available}>{backend.displayName}</button>
            {/each}
          </div>
        </div>
        <div class="settings-row">
          <div><p class="eyebrow">FUENTES</p><h3>{sources.length} fuentes disponibles</h3><p>{sources.map((source) => source.label).join(' · ') || 'No hay fuentes disponibles.'}</p></div>
          <span class="settings-value">{snapshot.backend.capabilities.sourceKinds.join(' / ') || 'Pendiente'}</span>
        </div>
        <div class="settings-row">
          <div><p class="eyebrow">DIAGNÓSTICO</p><h3>{doctor?.gpu ?? 'GPU pendiente de detección'}</h3><p>{snapshot.lastError?.message ?? snapshot.backend.note ?? doctor?.notes[0] ?? 'Ejecuta el diagnóstico para conocer las capacidades locales.'}</p></div>
          <button class="secondary-button" on:click={runDoctor} disabled={busy}>Ejecutar</button>
        </div>
      </section>
    {/if}

    {#if notice}
      <div class="toast" role={snapshot.lastError ? 'alert' : 'status'}>{notice}</div>
    {/if}
  </main>
</div>
