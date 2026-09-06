-- Phase 3: live capture gains (PipeWire per-stream volume on Linux,
-- software gain on Windows). 100 = unity.
INSERT OR IGNORE INTO settings (key, value) VALUES
  ('gain_game', '100'),
  ('gain_mic', '100'),
  ('mute_game', '0'),
  ('mute_mic', '0');
