import { useCallback, useEffect, useRef } from "react";
import { useTranslation } from "react-i18next";
import { invoke } from "@tauri-apps/api/core";

/**
 * Locale with a single owner: i18next.
 * No local useState (a copy per caller desyncs, as proven in testing).
 * Every consumer uses useTranslation, so all re-render on change.
 */
export function useLocale() {
  const { i18n } = useTranslation();
  const initialized = useRef(false);

  useEffect(() => {
    if (initialized.current) return;
    initialized.current = true;
    invoke<Record<string, string>>("get_settings")
      .then((s) => {
        if (s.locale && s.locale !== i18n.language) void i18n.changeLanguage(s.locale);
      })
      .catch(console.error);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const setLocale = useCallback(
    async (next: string) => {
      if (next === i18n.language) return;
      await i18n.changeLanguage(next);
      await invoke("set_setting", { key: "locale", value: next });
    },
    [i18n],
  );

  return { locale: i18n.language || "es", setLocale };
}
