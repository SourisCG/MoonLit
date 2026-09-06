import { useEffect, useState } from "react";
import { useTranslation } from "react-i18next";
import { invoke } from "@tauri-apps/api/core";

interface CodecOpt {
  id: string;
  label: string;
  note: string;
}

interface HeightOpt {
  height: number;
  label: string;
  bitrates: number[];
  ring_mb_60s: number[];
}

interface VideoOptions {
  codecs: CodecOpt[];
  heights: HeightOpt[];
  current_codec: string;
  current_height: number;
  current_fps: number;
  max_source_height: number;
  vendor: string;
}

/** Recording video: codec + output resolution (Medal ladder, GSR-backed). */
export function VideoSection() {
  const { t } = useTranslation();
  const [opts, setOpts] = useState<VideoOptions | null>(null);
  const [error, setError] = useState<string | null>(null);

  const load = () => {
    invoke<VideoOptions>("video_options").then(setOpts).catch((e) => setError(String(e)));
  };
  useEffect(load, []);

  const change = async (key: "video_codec" | "out_height" | "fps", value: string) => {
    setError(null);
    try {
      await invoke("set_setting", { key, value });
      load();
    } catch (e) {
      setError(String(e));
    }
  };

  if (error) return <p className="font-mono text-xs text-red-400">{error}</p>;
  if (!opts) return <p className="text-sm text-slate-400">{t("common.loading")}</p>;

  const codecIdx = Math.max(
    0,
    opts.codecs.findIndex((c) => c.id === opts.current_codec),
  );
  const heightRow =
    opts.heights.find((h) => h.height === opts.current_height) ?? opts.heights[2];
  const bitrate = heightRow.bitrates[Math.min(codecIdx, heightRow.bitrates.length - 1)];
  const ram = heightRow.ring_mb_60s[Math.min(codecIdx, heightRow.ring_mb_60s.length - 1)];
  const codecNote = opts.codecs[codecIdx]?.note ?? "";
  const upscale =
    opts.max_source_height > 0 &&
    opts.current_height > opts.max_source_height;

  const select =
    "w-full rounded-lg border border-white/10 bg-white/5 px-2.5 py-1.5 text-sm text-slate-100 outline-none focus:border-cyan-500/50";

  return (
    <div className="space-y-3">
      <div className="grid grid-cols-1 gap-3 sm:grid-cols-2">
        <label className="block">
          <span className="mb-1 block text-sm text-slate-300">{t("video.codec")}</span>
          <select
            className={select}
            value={opts.current_codec}
            onChange={(e) => void change("video_codec", e.target.value)}
          >
            {opts.codecs.map((c) => (
              <option key={c.id} value={c.id}>
                {c.label}
              </option>
            ))}
          </select>
        </label>
        <label className="block">
          <span className="mb-1 block text-sm text-slate-300">{t("video.resolution")}</span>
          <select
            className={select}
            value={String(opts.current_height)}
            onChange={(e) => void change("out_height", e.target.value)}
          >
            <option value="0">{t("video.source")}</option>
            {opts.heights.map((h) => (
              <option key={h.height} value={String(h.height)}>
                {h.label}
              </option>
            ))}
          </select>
        </label>
        <label className="block">
          <span className="mb-1 block text-sm text-slate-300">{t("video.fps")}</span>
          <select
            className={select}
            value={String(opts.current_fps)}
            onChange={(e) => void change("fps", e.target.value)}
          >
            <option value="60">60 fps</option>
            <option value="30">30 fps</option>
          </select>
        </label>
      </div>
      <p className="text-xs text-slate-400">{codecNote}</p>
      {upscale && (
        <p className="text-xs text-amber-400">{t("video.upscale_warn")}</p>
      )}
      <p className="font-mono text-xs text-slate-400">
        {t("video.estimate", { bitrate, ram })}
      </p>
      <p className="text-xs text-slate-600">{t("video.hq_note")}</p>
      <p className="text-xs text-slate-600">{t("video.judder_note")}</p>
    </div>
  );
}
