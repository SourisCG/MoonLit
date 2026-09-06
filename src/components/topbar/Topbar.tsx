import { useState } from "react";
import { useTranslation } from "react-i18next";
import { getCurrentWindow } from "@tauri-apps/api/window";
import { Minus, Square, X } from "lucide-react";
import { MoonlitLogo } from "../logo/MoonlitLogo";

/** Frameless custom topbar: drag region + brand + window controls (close hides to tray). */
export function Topbar() {
  const { t } = useTranslation();
  const [maximized, setMaximized] = useState(false);

  const win = () => getCurrentWindow();
  const stop = (e: React.SyntheticEvent) => e.stopPropagation();

  const onMinimize = (e: React.MouseEvent) => {
    stop(e);
    void win().minimize();
  };
  const onToggleMax = async (e: React.MouseEvent) => {
    stop(e);
    const w = win();
    const isMax = await w.isMaximized();
    if (isMax) await w.unmaximize();
    else await w.maximize();
    setMaximized(!isMax);
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
      <div className="flex items-center gap-1">
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
