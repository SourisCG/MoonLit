-- Phase 3: video codec + output height (0 = source resolution).
INSERT OR IGNORE INTO settings (key, value) VALUES
  ('video_codec', 'h264'),
  ('out_height', '0');
