<script lang="ts">
  import { onMount } from 'svelte';
  import { invoke } from '@tauri-apps/api/core';

  type CaptureStatus = 'idle' | 'buffering' | 'error';

  type ClipRecord = {
    id: string;
    path: string;
    createdAt: number;
    durationSeconds: number;
    kind: string;
  };

  type RuntimeSnapshot = {
    status: CaptureStatus;
    backend: string;
    sessionId: string | null;
    gameLabel: string | null;
    startedAt: number | null;
    bufferSeconds: number;
    savedClips: number;
    lastClip: ClipRecord | null;
    message: string;
  };

  type CommandProbe = {
    name: string;
    available: boolean;
    state: string;
    executable: string | null;
    exitCode: number | null;
    version: string | null;
    detail: string | null;
  };

  type NativeBackendStatus = {
    name: string;
    available: boolean;
    executable: string | null;
    origin: string;
    sha256: string | null;
    status: string;
    version: string | null;
    codecs: string[];
    note: string;
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

  const emptySnapshot: RuntimeSnapshot = {
    status: 'idle',
    backend: 'fake',
    sessionId: null,
    gameLabel: null,
    startedAt: null,
    bufferSeconds: 30,
    savedClips: 0,
    lastClip: null,
    message: 'Listo para iniciar una prueba.',
  };

  let snapshot = emptySnapshot;
  let doctor: DoctorReport | null = null;
  let nativeBackend: NativeBackendStatus | null = null;
  let bufferSeconds = 30;
  let externalGsrPath = '';
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

  function browserBackend(): NativeBackendStatus {
    return {
      name: 'gpu-screen-recorder',
      available: false,
      executable: null,
      origin: 'preview',
      sha256: null,
      status: 'preview',
      version: null,
      codecs: [],
      note: 'La vista web no puede consultar ni iniciar el backend nativo.',
    };
  }

  async function refreshSnapshot() {
    if (!isTauri) return;
    snapshot = await invoke<RuntimeSnapshot>('get_runtime_snapshot');
  }

  async function refreshBackend() {
    try {
      nativeBackend = isTauri
        ? await invoke<NativeBackendStatus>('get_capture_backend')
        : browserBackend();
    } catch (error) {
      nativeBackend = null;
      notice = `No se pudo consultar el backend nativo: ${String(error)}`;
    }
  }

  async function runDoctor() {
    busy = true;
    notice = '';
    try {
      doctor = isTauri ? await invoke<DoctorReport>('run_doctor') : browserDoctor();
      await refreshBackend();
      notice = 'Diagnóstico actualizado.';
    } catch (error) {
      notice = `No se pudo ejecutar el diagnóstico: ${String(error)}`;
    } finally {
      busy = false;
    }
  }

  async function startCapture() {
    busy = true;
    notice = '';
    try {
      snapshot = isTauri
        ? await invoke<RuntimeSnapshot>('start_capture', { bufferSeconds })
        : {
            ...snapshot,
            status: 'buffering',
            bufferSeconds,
            sessionId: `preview-${Date.now()}`,
            gameLabel: 'Simulación MoonLit',
            startedAt: Math.floor(Date.now() / 1000),
            message: 'Buffer simulado activo en la vista previa.',
          };
      notice = snapshot.backend === 'fake' ? 'Buffer simulado iniciado.' : 'Buffer GSR iniciado.';
    } catch (error) {
      notice = `No se pudo iniciar el buffer: ${String(error)}`;
    } finally {
      busy = false;
    }
  }

  async function saveClip() {
    busy = true;
    notice = '';
    try {
      if (snapshot.status !== 'buffering') {
        throw new Error('Inicia el buffer antes de guardar un clip');
      }
      snapshot = isTauri
        ? await invoke<RuntimeSnapshot>('save_clip')
        : {
            ...snapshot,
            savedClips: snapshot.savedClips + 1,
            lastClip: {
              id: `preview-${Date.now()}`,
              path: 'vista previa / clip simulado',
              createdAt: Math.floor(Date.now() / 1000),
              durationSeconds: bufferSeconds,
              kind: 'simulation',
            },
            message: 'Clip simulado guardado en la vista previa.',
          };
      notice = snapshot.backend === 'fake' ? 'Clip simulado guardado.' : 'Clip nativo guardado.';
    } catch (error) {
      notice = `No se pudo guardar el clip: ${String(error)}`;
    } finally {
      busy = false;
    }
  }

  async function stopCapture() {
    busy = true;
    notice = '';
    try {
      snapshot = isTauri
        ? await invoke<RuntimeSnapshot>('stop_capture')
        : {
            ...snapshot,
            status: 'idle',
            sessionId: null,
            gameLabel: null,
            startedAt: null,
            message: 'Buffer simulado detenido.',
          };
      notice = 'Buffer detenido.';
    } catch (error) {
      notice = `No se pudo detener el buffer: ${String(error)}`;
    } finally {
      busy = false;
    }
  }

  function formatTime(timestamp: number | null) {
    if (!timestamp) return 'Nunca';
    return new Date(timestamp * 1000).toLocaleString('es-ES', {
      dateStyle: 'medium',
      timeStyle: 'short',
    });
  }

  function command(name: string) {
    return doctor?.commands.find((item) => item.name === name);
  }

  async function selectBackend(name: 'fake' | 'gpu-screen-recorder') {
    busy = true;
    notice = '';
    try {
      if (!isTauri) {
        snapshot = { ...snapshot, backend: 'fake' };
        notice = 'La vista web sólo admite el backend simulado.';
        return;
      }
      snapshot = await invoke<RuntimeSnapshot>('set_capture_backend', { backend: name });
      notice = name === 'fake' ? 'Backend simulado seleccionado.' : 'Backend GSR seleccionado.';
    } catch (error) {
      notice = `No se pudo cambiar el backend: ${String(error)}`;
    } finally {
      busy = false;
    }
  }

  async function selectExternalBackend() {
    busy = true;
    notice = '';
    try {
      if (!isTauri) {
        throw new Error('La vista web no puede seleccionar ejecutables externos');
      }
      if (!externalGsrPath.trim()) {
        throw new Error('Indica una ruta absoluta a gpu-screen-recorder');
      }
      snapshot = await invoke<RuntimeSnapshot>('set_external_capture_backend', {
        path: externalGsrPath.trim(),
      });
      notice = 'Backend GSR externo seleccionado.';
    } catch (error) {
      notice = `No se pudo seleccionar GSR externo: ${String(error)}`;
    } finally {
      busy = false;
    }
  }

  onMount(async () => {
    await refreshSnapshot();
    await runDoctor();
  });
</script>

<svelte:head>
  <title>MoonLit | Clips Windows</title>
</svelte:head>

<div class="app-shell">
  <aside class="sidebar">
    <div class="brand">
      <div class="brand-mark">S</div>
      <div>
        <strong>MoonLit</strong>
        <span>clips para Linux</span>
      </div>
    </div>

    <nav aria-label="Navegación principal">
      <button class:active={activeView === 'overview'} on:click={() => (activeView = 'overview')}>
        <span class="nav-icon">◈</span>
        Resumen
      </button>
      <button class:active={activeView === 'library'} on:click={() => (activeView = 'library')}>
        <span class="nav-icon">▦</span>
        Biblioteca
        <span class="nav-count">{snapshot.savedClips}</span>
      </button>
      <button class:active={activeView === 'settings'} on:click={() => (activeView = 'settings')}>
        <span class="nav-icon">⚙</span>
        Ajustes
      </button>
    </nav>

    <div class="sidebar-bottom">
      <div class="connection-dot"><span></span> {snapshot.backend === 'fake' ? 'Motor simulado' : 'Motor GSR'}</div>
      <small>Fase 2 · Viabilidad</small>
    </div>
  </aside>

  <main class="main-content">
    <header class="topbar">
      <div>
        <p class="eyebrow">CENTRO DE CONTROL</p>
        <h1>{activeView === 'overview' ? 'Tu momento, guardado.' : activeView === 'library' ? 'Biblioteca local' : 'Ajustes del sistema'}</h1>
      </div>
      <div class="topbar-actions">
        <span class:live={snapshot.status === 'buffering'} class="status-pill">
          <span class="status-dot"></span>
          {snapshot.status === 'buffering' ? 'Buffer activo' : 'En espera'}
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
          <p class="hero-description">Prueba el flujo de clips ahora. El motor simulado permite trabajar sin GPU; GSR sólo se activa de forma explícita cuando el sistema pasa su diagnóstico.</p>
          </div>
          <div class="hero-orbit orbit-one"></div>
          <div class="hero-orbit orbit-two"></div>
          <div class="hero-orbit orbit-three"></div>
          <div class="hero-corner-label">LOCAL FIRST <span>01</span></div>
        </div>

        <div class="capture-card">
          <div class="card-heading">
            <div>
              <p class="eyebrow">PRUEBA DE CAPTURA</p>
              <h3>Buffer de clips</h3>
            </div>
            <span class="recording-indicator" class:recording={snapshot.status === 'buffering'}></span>
          </div>

          <label for="buffer-length">Duración del clip</label>
          <div class="duration-control">
            <input id="buffer-length" type="range" min="10" max="300" step="10" bind:value={bufferSeconds} disabled={snapshot.status === 'buffering'} />
            <strong>{bufferSeconds}<small>s</small></strong>
          </div>
          <div class="range-labels"><span>10 s</span><span>5 min</span></div>

          <div class="capture-actions">
            {#if snapshot.status === 'buffering'}
              <button class="primary-button" on:click={saveClip} disabled={busy}><span>●</span> Guardar clip</button>
              <button class="secondary-button" on:click={stopCapture} disabled={busy}>Detener</button>
            {:else}
              <button class="primary-button" on:click={startCapture} disabled={busy}><span>▶</span> Iniciar buffer</button>
              <button class="secondary-button" on:click={runDoctor} disabled={busy}>Diagnóstico</button>
            {/if}
          </div>
          <p class="card-footnote">{snapshot.backend === 'fake' ? 'El manifest guardado es simulado y no contiene vídeo.' : 'La captura nativa requiere consentimiento del portal en Wayland.'}</p>
        </div>
      </section>

      <section class="stats-grid">
        <article class="stat-card">
          <span class="stat-label">SESIÓN ACTUAL</span>
          <strong>{snapshot.gameLabel ?? 'Ningún juego detectado'}</strong>
          <span class="stat-detail">{snapshot.startedAt ? `Desde ${formatTime(snapshot.startedAt)}` : 'Esperando actividad'}</span>
        </article>
        <article class="stat-card highlight-stat">
          <span class="stat-label">CLIPS GUARDADOS</span>
          <strong>{snapshot.savedClips}</strong>
          <span class="stat-detail">En esta sesión de prueba</span>
        </article>
          <article class="stat-card">
            <span class="stat-label">BACKEND</span>
            <strong>{snapshot.backend === 'fake' ? 'Simulado' : 'GSR nativo'}</strong>
            <span class="stat-detail">{snapshot.backend === 'fake' ? 'Predeterminado seguro' : 'Proceso supervisado'}</span>
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
              <span class="ready-badge">{doctor.capabilities.length} capacidades</span>
            </div>
            <div class="capability-list">
              <div><span class:ok={doctor.waylandDisplay} class="cap-dot"></span><span>Wayland</span><strong>{doctor.waylandDisplay ? 'Detectado' : 'No detectado'}</strong></div>
              <div><span class:ok={doctor.x11Display} class="cap-dot"></span><span>X11</span><strong>{doctor.x11Display ? 'Detectado' : 'No detectado'}</strong></div>
              <div><span class:ok={Boolean(command('pipewire-graph')?.available)} class="cap-dot"></span><span>PipeWire</span><strong>{command('pipewire-graph')?.available ? 'Disponible' : 'Pendiente'}</strong></div>
              <div><span class:ok={Boolean(nativeBackend?.available)} class="cap-dot"></span><span>Grabador GPU</span><strong>{nativeBackend?.status === 'ready' ? 'Listo' : nativeBackend?.status === 'degraded' ? 'Degradado' : 'Pendiente'}</strong></div>
            </div>
          {:else}
            <div class="empty-state">Ejecutando diagnóstico...</div>
          {/if}
        </article>

        <article class="panel last-clip-panel">
          <div class="panel-heading"><div><p class="eyebrow">ACTIVIDAD RECIENTE</p><h3>Último clip</h3></div><span class="panel-arrow">↗</span></div>
          {#if snapshot.lastClip}
            <div class="clip-preview"><div class="clip-art"><span>PLAY</span></div><div class="clip-info"><strong>Clip simulado</strong><span>{snapshot.lastClip.durationSeconds}s · {formatTime(snapshot.lastClip.createdAt)}</span><small>{snapshot.lastClip.path}</small></div></div>
          {:else}
            <div class="empty-state"><span class="empty-icon">◇</span><span>Guarda tu primer clip de prueba<br />y aparecerá aquí.</span></div>
          {/if}
        </article>
      </section>
    {:else if activeView === 'library'}
      <section class="page-panel"><div class="empty-large"><span class="empty-icon">▦</span><h2>Biblioteca en construcción</h2><p>La biblioteca SQLite y sus filtros serán el siguiente módulo. Por ahora puedes generar clips simulados desde el resumen.</p><button class="primary-button" on:click={() => (activeView = 'overview')}>Volver al resumen</button></div></section>
    {:else}
      <section class="page-panel settings-page">
        <div class="settings-row">
          <div><p class="eyebrow">MODO DE CAPTURA</p><h3>Backend actual</h3><p>El backend simulado es seguro para desarrollo. GSR sólo se ejecuta tras seleccionarlo y comprobar que está disponible.</p></div>
          <div class="backend-choice">
            <button class:chosen={snapshot.backend === 'fake'} class="backend-button" on:click={() => selectBackend('fake')} disabled={busy || snapshot.status === 'buffering'}>Simulado</button>
            <button class:chosen={snapshot.backend === 'gpu-screen-recorder'} class="backend-button" on:click={() => selectBackend('gpu-screen-recorder')} disabled={busy || snapshot.status === 'buffering' || !nativeBackend?.available}>GSR nativo</button>
          </div>
        </div>
        <div class="settings-row">
          <div><p class="eyebrow">BACKEND EXTERNO</p><h3>Ruta personalizada</h3><p>Úsalo para probar otra versión de GSR sin reemplazar el componente incluido.</p></div>
          <div class="external-choice"><input class="backend-input" type="text" bind:value={externalGsrPath} placeholder="/ruta/a/gpu-screen-recorder" aria-label="Ruta externa de GSR" /><button class="secondary-button" on:click={selectExternalBackend} disabled={busy || snapshot.status === 'buffering'}>Usar ruta</button></div>
        </div>
        <div class="settings-row">
          <div><p class="eyebrow">DIAGNÓSTICO</p><h3>{doctor?.gpu ?? 'GPU pendiente de detección'}</h3><p>{nativeBackend?.note ?? doctor?.notes[0] ?? 'Ejecuta el diagnóstico para conocer las capacidades locales.'}</p>{#if nativeBackend}<small class="backend-meta">{nativeBackend.version ?? 'Versión pendiente'} · Origen: {nativeBackend.origin} · {nativeBackend.codecs.length ? `Codecs: ${nativeBackend.codecs.join(', ')}` : 'Codecs no reportados'}</small>{/if}</div>
          <button class="secondary-button" on:click={runDoctor} disabled={busy}>Ejecutar</button>
        </div>
      </section>
    {/if}

    {#if notice}
      <div class="toast" role="status">{notice}</div>
    {/if}
  </main>
</div>
