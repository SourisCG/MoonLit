import { useEffect, useState } from "react";
import { useTranslation } from "react-i18next";
import { invoke } from "@tauri-apps/api/core";
import { TrackMixer } from "./TrackMixer";
import type { EngineStatus } from "../../hooks/useEngine";

interface AudioDevice {
  id: string;
  description: string;
  kind: string;
}

/** Capture audio: device selectors + live gains + link status (Settings section). */
export function AudioSection({ status }: { status: EngineStatus }) {
  const { t } = useTranslation();
  const [devices, setDevices] = useState<AudioDevice[]>([]);
  const [mic, setMic] = useState("default_input");
  const [desktop, setDesktop] = useState("default_output");
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    invoke<AudioDevice[]>("list_audio_devices").then(setDevices).catch((e) => setError(String(e)));
    invoke<Record<string, string>>("get_settings")
      .then((s) => {
        if (s.mic_device) setMic(s.mic_device);
        if (s.desktop_device) setDesktop(s.desktop_device);
      })
      .catch(console.error);
  }, []);

  const changeDevice = async (key: "mic_device" | "desktop_device", value: string) => {
    setError(null);
    try {
      await invoke("set_setting", { key, value });
      if (key === "mic_device") setMic(value);
      else setDesktop(value);
    } catch (e) {
      setError(String(e));
    }
  };

  const mics = devices.filter((d) => d.kind === "mic");
  const desktops = devices.filter((d) => d.kind === "desktop");
  const select =
    "w-full rounded-lg border border-white/10 bg-white/5 px-2.5 py-1.5 text-sm text-slate-100 outline-none focus:border-cyan-500/50";

  return (
    <div className="space-y-3">
      <div className="grid grid-cols-1 gap-3 sm:grid-cols-2">
        <label className="block">
          <span className="mb-1 block text-sm text-slate-300">{t("audio.mic_src")}</span>
          <select className={select} value={mic} onChange={(e) => void changeDevice("mic_device", e.target.value)}>
            {!mics.some((d) => d.id === mic) && <option value={mic}>{mic}</option>}
            {mics.map((d) => (
              <option key={d.id} value={d.id}>
                {d.description}
              </option>
            ))}
          </select>
        </label>
        <label className="block">
          <span className="mb-1 block text-sm text-slate-300">{t("audio.desktop_src")}</span>
          <select
            className={select}
            value={desktop}
            onChange={(e) => void changeDevice("desktop_device", e.target.value)}
          >
            {!desktops.some((d) => d.id === desktop) && <option value={desktop}>{desktop}</option>}
            {desktops.map((d) => (
              <option key={d.id} value={d.id}>
                {d.description}
              </option>
            ))}
          </select>
        </label>
      </div>

      <TrackMixer />

      <p className="font-mono text-xs text-slate-400">
        {status.running
          ? t("audio.linked", { n: status.tracks_linked })
          : t("audio.stopped_hint")}
        {status.audio_error && <span className="text-red-400"> · {status.audio_error}</span>}
        {error && <span className="text-red-400"> · {error}</span>}
      </p>
    </div>
  );
}
