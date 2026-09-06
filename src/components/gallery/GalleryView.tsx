import { useEffect, useState } from "react";
import { useTranslation } from "react-i18next";
import { convertFileSrc, invoke } from "@tauri-apps/api/core";
import { openPath, revealItemInDir } from "@tauri-apps/plugin-opener";
import { Clapperboard, FolderOpen, Star, Trash2 } from "lucide-react";
import { useClips } from "../../hooks/useClips";
import type { ClipMetadata } from "../../types";

async function absOf(fileName: string): Promise<string> {
  return invoke<string>("resolve_clip_src", { file_name: fileName });
}

function Thumb({
  clip,
  onOpen,
  onError,
}: {
  clip: ClipMetadata;
  onOpen: () => void;
  onError: (msg: string) => void;
}) {
  const [src, setSrc] = useState<string | null>(null);
  useEffect(() => {
    let cancelled = false;
    absOf(clip.thumbnail_name).then(
      (abs) => {
        if (!cancelled) setSrc(convertFileSrc(abs));
      },
      (e) => {
        if (!cancelled) {
          onError(`thumb resolve: ${String(e)}`);
          setSrc(null);
        }
      },
    );
    return () => {
      cancelled = true;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [clip.thumbnail_name]);
  if (!src) {
    return (
      <button
        onClick={onOpen}
        className="flex h-16 w-full items-center justify-center rounded-lg bg-black/40 transition hover:bg-black/60 sm:w-28"
        title={clip.file_name}
      >
        <Clapperboard size={18} className="text-slate-600" />
      </button>
    );
  }
  return (
    <button onClick={onOpen} title={clip.file_name} className="w-full shrink-0 sm:w-auto">
      <img
        src={src}
        alt=""
        onError={() => onError(`thumb asset blocked: ${clip.thumbnail_name}`)}
        className="h-16 w-full rounded-lg object-cover transition hover:brightness-125 sm:w-28"
      />
    </button>
  );
}

function fmtDuration(ms: number) {
  const s = Math.round(ms / 1000);
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, "0")}`;
}

function timeNow() {
  return new Date().toLocaleTimeString();
}

interface RowActions {
  onToggleFavorite: (id: string) => void;
  onDelete: (id: string) => void;
  onError: (msg: string) => void;
  onSuccess: () => void;
}

function ClipRow({ clip, actions }: { clip: ClipMetadata; actions: RowActions }) {
  const { t } = useTranslation();
  const openVideo = async () => {
    try {
      await openPath(await absOf(clip.file_name));
      actions.onSuccess();
    } catch (e) {
      actions.onError(`open: ${String(e)}`);
    }
  };
  const reveal = async () => {
    try {
      const abs = await absOf(clip.file_name);
      try {
        // Primary: select the file (needs a FileManager1 owner on the bus;
        // often missing on KDE without Dolphin running).
        await revealItemInDir(abs);
      } catch {
        // Fallback: plain-open the containing folder (xdg-open, guaranteed).
        const sep = abs.includes("\\") ? "\\" : "/";
        await openPath(abs.slice(0, abs.lastIndexOf(sep)));
      }
      actions.onSuccess();
    } catch (e) {
      actions.onError(`reveal: ${String(e)}`);
    }
  };

  const iconBtn =
    "rounded-lg p-1.5 text-slate-500 transition hover:bg-white/10 hover:text-slate-200";

  return (
    <li className="rounded-xl border border-white/5 bg-black/30 p-2.5 pr-4">
      <div className="flex items-center gap-3">
        <Thumb clip={clip} onOpen={() => void openVideo()} onError={actions.onError} />
        <div className="min-w-0 flex-1 text-sm">
          <p className="truncate font-medium text-slate-200">
            {clip.game_title}{" "}
            <span className="font-mono text-xs text-cyan-300">{fmtDuration(clip.duration_ms)}</span>{" "}
            {!clip.exists && <span className="text-xs text-amber-400">({t("gallery.missing")})</span>}
          </p>
          <p className="truncate font-mono text-xs text-slate-500">{clip.file_name}</p>
        </div>
        <div className="flex shrink-0 items-center gap-1">
          <button onClick={() => void reveal()} className={iconBtn} title={t("gallery.reveal")}>
            <FolderOpen size={15} />
          </button>
          <button
            onClick={() => actions.onToggleFavorite(clip.id)}
            className={`rounded-lg p-1.5 transition ${clip.is_favorite ? "text-amber-300" : "text-slate-500 hover:text-amber-200"}`}
            title={t("gallery.favorite")}
          >
            <Star size={15} fill={clip.is_favorite ? "currentColor" : "none"} />
          </button>
          <button
            onClick={() => actions.onDelete(clip.id)}
            className="rounded-lg p-1.5 text-slate-500 transition hover:bg-red-500/20 hover:text-red-300"
            title={t("common.delete")}
          >
            <Trash2 size={15} />
          </button>
        </div>
      </div>
    </li>
  );
}

export function GalleryView({ refreshToken }: { refreshToken: number }) {
  const { t } = useTranslation();
  // Single shared instance: rows act on THIS list (a per-row instance would
  // refresh a phantom copy and the UI would look dead).
  const { clips, loading, refresh, toggleFavorite, deleteClip } = useClips();
  const [lastError, setLastError] = useState<string | null>(null);

  useEffect(() => {
    if (refreshToken > 0) void refresh();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [refreshToken]);

  const fail = (msg: string) => setLastError(`${timeNow()} · ${msg}`);
  const actions: RowActions = {
    onToggleFavorite: (id) =>
      toggleFavorite(id).then(
        () => setLastError(null),
        (e) => fail(String(e)),
      ),
    onDelete: (id) =>
      deleteClip(id).then(
        () => setLastError(null),
        (e) => fail(String(e)),
      ),
    onError: fail,
    onSuccess: () => setLastError(null),
  };

  if (loading && clips.length === 0)
    return <p className="text-sm text-slate-400">{t("common.loading")}</p>;

  if (clips.length === 0) {
    return (
      <>
        <p className="mt-1 text-sm text-slate-400">{t("gallery.coming")}</p>
        <div className="mt-6 grid grid-cols-1 gap-4 sm:grid-cols-2 xl:grid-cols-3">
          {[0, 1, 2].map((i) => (
            <div
              key={i}
              className="relative overflow-hidden rounded-xl border border-dashed border-white/10 bg-moonlit-card/60 p-6 text-center"
            >
              <Clapperboard size={22} className="mx-auto text-slate-600" />
              <p className="mt-2 text-xs text-slate-500">{t("gallery.empty")}</p>
            </div>
          ))}
        </div>
      </>
    );
  }

  return (
    <>
      {lastError && (
        <p className="mb-2 truncate font-mono text-xs text-red-400" title={lastError}>
          {lastError}
        </p>
      )}
      <ul className="mt-2 space-y-2">
        {clips.map((c) => (
          <ClipRow key={c.id} clip={c} actions={actions} />
        ))}
      </ul>
    </>
  );
}
