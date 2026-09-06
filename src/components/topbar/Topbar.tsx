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
        // TEMP-DEBUG (maximize flicker investigation, remove after fix)
        console.debug(`[moonlit-dbg] onResized fired, isMaximized=${isMax} t=${Date.now()}`);
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

  const onMinimize = (e: React.MouseEvent) => {
    stop(e);
    void win().minimize();
  };
  const onToggleMax = async (e: React.MouseEvent) => {
    e.stopPropagation();
    e.preventDefault();
    const now = Date.now();
    // TEMP-DEBUG (maximize flicker investigation, remove after fix)
    console.debug(`[moonlit-dbg] onToggleMax called t=${now} type=${e.type}`);
    if (now - lastToggle.current < TOGGLE_DEBOUNCE_MS) {
      console.debug(`[moonlit-dbg] onToggleMax DEBOUNCED t=${now}`);
      return;
    }
    lastToggle.current = now;
    const w = win();
    try {
      if (await w.isMaximized()) await w.unmaximize();
      else await w.maximize();
      setMaximized(await w.isMaximized());
    } catch (err) {
      console.error(err);
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
        onDoubleClick={(e) => void onToggleMax(e)}
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
          onClick={(e) => void onToggleMax(e)}
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
