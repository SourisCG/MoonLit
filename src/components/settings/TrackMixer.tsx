import { useEffect, useRef, useState } from "react";
import { useTranslation } from "react-i18next";
import { invoke } from "@tauri-apps/api/core";
import { Volume2, VolumeX } from "lucide-react";

interface Gains {
  game: number;
  mic: number;
  mute_game: boolean;
  mute_mic: boolean;
}

type Track = "game" | "mic";

/** Live capture gain: PipeWire per-stream volume (Linux). Never touches monitoring. */
export function TrackMixer() {
  const { t } = useTranslation();
  const [gains, setGains] = useState<Gains>({ game: 100, mic: 100, mute_game: false, mute_mic: false });
  const timer = useRef<number | undefined>(undefined);

  useEffect(() => {
    invoke<Gains>("audio_levels").then(setGains).catch(console.error);
    return () => window.clearTimeout(timer.current);
  }, []);

  const gainsRef = useRef(gains);
  gainsRef.current = gains;

  const commit = (track: Track, patch: Partial<Gains>) => {
    const next = { ...gainsRef.current, ...patch };
    gainsRef.current = next;
    setGains(next);
    window.clearTimeout(timer.current);
    timer.current = window.setTimeout(() => {
      if ("game" in patch || "mic" in patch) {
        const pct = track === "game" ? next.game : next.mic;
        invoke("set_track_gain", { track, percent: pct }).catch(console.error);
      } else {
        const muted = track === "game" ? next.mute_game : next.mute_mic;
        invoke("set_track_mute", { track, muted }).catch(console.error);
      }
    }, 250);
  };
  const row = (track: Track, value: number, muted: boolean) => (
    <div className="flex items-center gap-2">
      <button
        onClick={() =>
          commit(track, track === "game" ? { mute_game: !muted } : { mute_mic: !muted })
        }
        className={`rounded-md p-1 transition ${muted ? "text-red-400" : "text-slate-400 hover:text-slate-200"}`}
        title={track === "game" ? t("rec.game") : t("rec.mic")}
      >
        {muted ? <VolumeX size={13} /> : <Volume2 size={13} />}
      </button>
      <span className="w-8 text-[11px] text-slate-400">
        {track === "game" ? t("rec.game") : t("rec.mic")}
      </span>
      <input
        type="range"
        min={0}
        max={150}
        value={value}
        disabled={muted}
        onChange={(e) =>
          commit(track, track === "game" ? { game: Number(e.target.value) } : { mic: Number(e.target.value) })
        }
        className="h-1 flex-1 cursor-pointer appearance-none rounded-full bg-white/10 accent-cyan-400 disabled:opacity-40"
      />
      <span className="w-9 text-right font-mono text-[11px] text-slate-300">{value}%</span>
    </div>
  );

  return (
    <div className="space-y-1.5 rounded-xl border border-white/5 bg-black/30 p-2.5">
      {row("game", gains.game, gains.mute_game)}
      {row("mic", gains.mic, gains.mute_mic)}
    </div>
  );
}
