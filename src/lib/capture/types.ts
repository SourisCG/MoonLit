export type CapturePhase = 'idle' | 'starting' | 'buffering' | 'saving' | 'stopping' | 'faulted';
export type BackendId = 'fake' | 'libobsSidecar' | 'windowsNative' | 'legacyGsr';
export type SourceKind = 'monitor' | 'window';
export type EncoderId = 'auto' | 'nvenc' | 'amf' | 'quickSync' | 'software';
export type VideoCodec = 'h264' | 'hevc';
export type ContainerFormat = 'mp4' | 'mkv';
export type QualityPreset = 'low' | 'medium' | 'high' | 'ultra' | 'custom';

export type CaptureSource = {
  id: string;
  kind: SourceKind;
  label: string;
  isDefault: boolean;
  width: number | null;
  height: number | null;
  processName: string | null;
  available: boolean;
};

export type VideoResolution = { width: number; height: number };

export type AudioConfig = {
  systemEnabled: boolean;
  microphoneEnabled: boolean;
  systemDeviceId: string | null;
  microphoneDeviceId: string | null;
  systemGain: number;
  microphoneGain: number;
  systemMuted: boolean;
  microphoneMuted: boolean;
  bitrateKbps: number;
};

export type AudioCapabilities = {
  available: boolean;
  systemAudio: boolean;
  microphone: boolean;
  applicationAudio: boolean;
  note: string | null;
};

export type AudioDevice = {
  id: string;
  kind: 'system' | 'microphone';
  label: string;
  isDefault: boolean;
  available: boolean;
};

export type AudioMixerSnapshot = {
  revision: number;
  devices: AudioDevice[];
  config: AudioConfig;
  systemLevel: number;
  microphoneLevel: number;
  syncDriftMs: number | null;
  status: string;
};

export type EncoderCapability = {
  id: EncoderId;
  available: boolean;
  reason: string | null;
};

export type BackendDescriptor = {
  id: BackendId;
  displayName: string;
  available: boolean;
  simulated: boolean;
  capabilities: {
    sourceKinds: SourceKind[];
    maxResolution: VideoResolution | null;
    maxFps: number | null;
    encoders: EncoderCapability[];
    codecs: VideoCodec[];
    formats: ContainerFormat[];
    audio: AudioCapabilities;
  };
  note: string | null;
};

export type ReplayConfig = {
  sourceId: string;
  bufferSeconds: number;
  resolution: VideoResolution | null;
  fps: number | null;
  encoder: EncoderId;
  codec: VideoCodec;
  format: ContainerFormat;
  quality: QualityPreset;
  bitrateKbps: number | null;
  audio: AudioConfig;
};

export type EffectiveReplaySettings = {
  encoder: string;
  codec: VideoCodec;
  format: ContainerFormat;
};

export type CaptureError = {
  code: string;
  message: string;
  retryable: boolean;
};

export type ClipRecord = {
  id: string;
  path: string;
  createdAtMs: number;
  durationSeconds: number;
  kind: string;
  sizeBytes: number;
  codec: VideoCodec;
  format: ContainerFormat;
  width: number | null;
  height: number | null;
  fps: number | null;
  hasAudio: boolean;
  proxyPath: string | null;
  proxyStatus: string;
};

export type CaptureSnapshot = {
  revision: number;
  phase: CapturePhase;
  backend: BackendDescriptor;
  config: ReplayConfig | null;
  effective: EffectiveReplaySettings | null;
  canSave: boolean;
  session: {
    id: string;
    sourceId: string;
    sourceLabel: string;
    startedAtMs: number;
  } | null;
  savedClips: number;
  lastClip: ClipRecord | null;
  lastError: CaptureError | null;
};

export type AppConfig = {
  schemaVersion: number;
  backend: BackendId;
  replay: ReplayConfig;
  storageDir: string | null;
  hotkeys: { saveClip: string };
  minimizeToTray: boolean;
  startMinimized: boolean;
  notificationsEnabled: boolean;
  onboardingVersion: number;
};

export type ClipMetadata = ClipRecord & {
  title: string;
  tags: string[];
  favorite: boolean;
  fileStatus: string;
};

export type ClipUpdate = {
  title?: string;
  tags?: string[];
  favorite?: boolean;
};

export type StorageStats = {
  root: string;
  clipCount: number;
  bytesUsed: number;
  availableBytes: number | null;
};

export type RecorderEvent =
  | { type: 'stateChanged'; snapshot: CaptureSnapshot }
  | { type: 'clipSaved'; snapshot: CaptureSnapshot; clip: ClipRecord }
  | { type: 'errorOccurred'; snapshot: CaptureSnapshot; error: CaptureError };
