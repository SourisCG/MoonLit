export interface HotkeyEvent {
  shortcut: string;
  pressed_at: string;
}

export interface Phase1Status {
  hotkey: string;
  presses: number;
  lastPress: string | null;
}

/** Mirrors Rust ClipRecord. file_name is RELATIVE to the clips directory. */
export interface ClipMetadata {
  id: string;
  file_name: string;
  thumbnail_name: string;
  game_title: string;
  duration_ms: number;
  file_size_bytes: number;
  created_at: string;
  is_favorite: boolean;
  drive_file_id?: string | null;
  drive_web_url?: string | null;
  /** Computed: file still on disk? */
  exists: boolean;
}

export type AppSettings = Record<string, string>;

/** Mirrors Rust CustomApp. */
export interface CustomApp {
  id: string;
  display_name: string;
  target_exe: string;
  match_strategy: string;
  clip_duration_seconds?: number | null;
  icon_path?: string | null;
  is_wine_proton: boolean;
}

export interface RegisterAppInput {
  display_name: string;
  target_exe: string;
  match_strategy: string;
  clip_duration_seconds?: number | null;
  is_wine_proton?: boolean | null;
}
