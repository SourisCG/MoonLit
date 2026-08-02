import type { CaptureSnapshot, ClipMetadata, EffectiveReplaySettings } from './types';

/**
 * Recorder events and command responses can arrive in either order.  Once a
 * host snapshot has been accepted, only a strictly newer revision may replace
 * it.  The first host snapshot is allowed to have revision zero.
 */
export function acceptsSnapshotRevision(
  currentRevision: number | null,
  nextRevision: number,
): boolean {
  return currentRevision === null || nextRevision > currentRevision;
}

/** A path is trusted by the UI only after the host has classified it present. */
export function isPlayableFileStatus(fileStatus: string | null | undefined): boolean {
  return fileStatus?.trim().toLowerCase() === 'present';
}

/** FakeBackend artifacts are manifests, never media playable by WebView2. */
export function isSimulationClip(clip: Pick<ClipMetadata, 'kind'>): boolean {
  return clip.kind.trim().toLowerCase() === 'simulation';
}

/**
 * Effective settings are the only settings that describe an active recorder.
 * Keeping this small adapter pure makes it difficult for the UI to regress to
 * showing the requested config as if the host had accepted it.
 */
export function effectiveSettingsForDisplay(
  snapshot: Pick<CaptureSnapshot, 'effective'>,
): EffectiveReplaySettings | null {
  return snapshot.effective;
}
