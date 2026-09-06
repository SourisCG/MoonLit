-- Phase 3: selectable capture devices (GSR -a args).
INSERT OR IGNORE INTO settings (key, value) VALUES
  ('mic_device', 'default_input'),
  ('desktop_device', 'default_output');
