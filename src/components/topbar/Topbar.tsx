import { useEffect, useRef, useState } from "react";
import { useTranslation } from "react-i18next";
import { getCurrentWindow } from "@tauri-apps/api/window";
import { Minus, Square, X } from "lucide-react";
import { MoonlitLogo } from "../logo/MoonlitLogo";

/** Ignore toggle requests closer than this (double-click fires click+click+dblclick). */
const TOGGLE_DEBOUNCE_MS = 500;

/** Frameless custom topbar: drag region + brand + window controls (close hides to tray). */
export function Topbar() {
  const { t } = useTranslation();
  // Mirrors the OS state (synced via resize events), never toggled blindly.
  const [maximized, setMaximized] = useState(false);
  const lastToggle = useRef(0);

  useEffect(() => {
    let cancelled = false;
    const sync = async () => {
      try {
        const isMax = await getCurrentWindow().isMaximized();
        if (!cancelled) setMaximized(isMax);
      } catch (err) {
        console.error(err);
      }
    };
    void sync();
    let unlisten: (() => void) | undefined;
    (async () => {
      try {
        const fn = await getCurrentWindow().onResized(() => void sync());
        if (cancelled) fn();
        else unlisten = fn;
      } catch (err) {
        console.error(err);
      }
    })();
    return () => {
      cancelled = true;
      unlisten?.();
    };
  }, []);

  const win = () => getCurrentWindow();
  const stop = (e: React.SyntheticEvent) => e.stopPropagation();
  const pendingTimer = useRef<number | undefined>(undefined);

  useEffect(() => () => window.clearTimeout(pendingTimer.current), []);

  const onMinimize = (e: React.MouseEvent) => {
    stop(e);
    void win().minimize();
  };

  const doToggle = async () => {
    const w = win();
    try {
      // Atomic toggle: no check-then-act race between rapid invocations.
      await w.toggleMaximize();
      setMaximized(await w.isMaximized());
    } catch (err) {
      console.error(err);
    }
  };

  /**
   * Double-click on the drag area also ends a native move-drag; maximizing
   * while that drag is settling makes Mutter revert it ~0.5s later.
   * Delaying lets the drag die first. Buttons toggle immediately.
   */
  const onToggleMax = (e: React.MouseEvent, delayMs = 0) => {
    e.stopPropagation();
    e.preventDefault();
    const now = Date.now();
    if (now - lastToggle.current < TOGGLE_DEBOUNCE_MS) return;
    lastToggle.current = now;
    window.clearTimeout(pendingTimer.current);
    if (delayMs <= 0) {
      void doToggle();
    } else {
      pendingTimer.current = window.setTimeout(() => void doToggle(), delayMs);
    }
  };
  const onClose = (e: React.MouseEvent) => {
    stop(e);
    // CloseRequested is intercepted in Rust -> hide to tray.
    void win().close();
  };

  return (
    <header className="relative z-20 flex h-10 select-none items-center justify-between border-b border-white/5 bg-[#0d0f14]/85 pl-3 pr-2 backdrop-blur-md">
      <div
        data-tauri-drag-region
        className="flex h-full flex-1 items-center gap-2"
        onDoubleClick={(e) => onToggleMax(e, 150)}
      >
        <MoonlitLogo size={18} />
        <span className="text-sm font-extrabold tracking-wide text-slate-100">
          Moon<span className="text-indigo-400">Lit</span>
        </span>
        <span className="hidden text-[11px] text-slate-500 sm:inline">{t("app.tagline")}</span>
      </div>
      <div className="flex items-center gap-1" onDoubleClick={stop}>
        <button
          aria-label={t("window.minimize")}
          title={t("window.minimize")}
          onPointerDown={stop}
          onClick={onMinimize}
          className="rounded-md p-1.5 text-slate-400 transition hover:bg-white/10 hover:text-slate-100"
        >
          <Minus size={15} />
        </button>
        <button
          aria-label={maximized ? t("window.restore") : t("window.maximize")}
          title={maximized ? t("window.restore") : t("window.maximize")}
          onPointerDown={stop}
          onClick={(e) => onToggleMax(e)}
          className="rounded-md p-1.5 text-slate-400 transition hover:bg-white/10 hover:text-slate-100"
        >
          <Square size={13} />
        </button>
        <button
          aria-label={t("window.close")}
          title={t("window.close")}
          onPointerDown={stop}
          onClick={onClose}
          className="rounded-md p-1.5 text-slate-400 transition hover:bg-red-500/80 hover:text-white"
        >
          <X size={15} />
        </button>
      </div>
    </header>
  );
}
