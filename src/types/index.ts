export interface HotkeyEvent {
  shortcut: string;
  pressed_at: string;
}

export interface Phase1Status {
  hotkey: string;
  presses: number;
  lastPress: string | null;
}
