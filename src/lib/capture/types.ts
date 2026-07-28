export type CapturePhase = 'idle' | 'starting' | 'buffering' | 'saving' | 'stopping' | 'faulted';
export type BackendId = 'fake' | 'windowsNative' | 'legacyGsr';
export type SourceKind = 'monitor' | 'window';
export type EncoderId = 'auto' | 'nvenc' | 'amf' | 'quickSync' | 'software';

export type CaptureSource = {
  id: string;
  kind: SourceKind;
  label: string;
  isDefault: boolean;
};

export type VideoResolution = { width: number; height: number };

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
  };
  note: string | null;
};

export type ReplayConfig = {
  sourceId: string;
  bufferSeconds: number;
  resolution: VideoResolution | null;
  fps: number | null;
  encoder: EncoderId;
  codec: 'h264' | 'hevc';
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
};

export type CaptureSnapshot = {
  revision: number;
  phase: CapturePhase;
  backend: BackendDescriptor;
  config: ReplayConfig | null;
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

export type RecorderEvent =
  | { type: 'stateChanged'; snapshot: CaptureSnapshot }
  | { type: 'clipSaved'; snapshot: CaptureSnapshot; clip: ClipRecord }
  | { type: 'errorOccurred'; snapshot: CaptureSnapshot; error: CaptureError };
