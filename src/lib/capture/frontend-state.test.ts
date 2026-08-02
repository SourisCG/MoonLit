import { describe, expect, it } from 'vitest';

import type { ClipMetadata } from './types';
import {
  acceptsSnapshotRevision,
  effectiveSettingsForDisplay,
  isPlayableFileStatus,
  isSimulationClip,
} from './frontend-state';

const clip = (overrides: Partial<ClipMetadata> = {}): ClipMetadata => ({
  id: 'clip-1',
  title: 'Clip',
  path: 'C:/Videos/clip.mp4',
  createdAtMs: 1,
  durationSeconds: 10,
  kind: 'recording',
  sizeBytes: 1,
  codec: 'h264',
  format: 'mp4',
  width: null,
  height: null,
  fps: null,
  hasAudio: false,
  proxyPath: null,
  proxyStatus: 'notNeeded',
  tags: [],
  favorite: false,
  fileStatus: 'present',
  ...overrides,
});

describe('frontend capture state guards', () => {
  it('accepts the first snapshot and only strictly newer revisions after it', () => {
    expect(acceptsSnapshotRevision(null, 0)).toBe(true);
    expect(acceptsSnapshotRevision(4, 4)).toBe(false);
    expect(acceptsSnapshotRevision(4, 3)).toBe(false);
    expect(acceptsSnapshotRevision(4, 5)).toBe(true);
  });

  it('trusts only host-classified present files for playback', () => {
    expect(isPlayableFileStatus('present')).toBe(true);
    expect(isPlayableFileStatus(' PRESENT ')).toBe(true);
    expect(isPlayableFileStatus('missing')).toBe(false);
    expect(isPlayableFileStatus('unsafe')).toBe(false);
    expect(isPlayableFileStatus('unknown')).toBe(false);
  });

  it('identifies simulation manifests before any media URL is created', () => {
    expect(isSimulationClip(clip({ kind: 'simulation' }))).toBe(true);
    expect(isSimulationClip(clip({ kind: 'recording' }))).toBe(false);
  });

  it('returns host effective settings without substituting requested values', () => {
    const effective = { encoder: 'software', codec: 'hevc' as const, format: 'mkv' as const };
    expect(effectiveSettingsForDisplay({ effective })).toEqual(effective);
    expect(effectiveSettingsForDisplay({ effective: null })).toBeNull();
  });
});
