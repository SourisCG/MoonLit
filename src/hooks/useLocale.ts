import { useCallback, useEffect, useState } from "react";
import { useTranslation } from "react-i18next";
import { invoke } from "@tauri-apps/api/core";

/** Single source of truth for UI locale: DB setting + i18next, always in sync. */
export function useLocale() {
  const { i18n } = useTranslation();
  const [locale, setLocaleState] = useState<string>(i18n.language || "es");

  useEffect(() => {
    invoke<Record<string, string>>("get_settings")
      .then((s) => {
        if (s.locale && s.locale !== i18n.language) void i18n.changeLanguage(s.locale);
        setLocaleState(s.locale || i18n.language);
      })
      .catch(console.error);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const setLocale = useCallback(
    async (next: string) => {
      setLocaleState(next);
      await i18n.changeLanguage(next);
      await invoke("set_setting", { key: "locale", value: next });
    },
    [i18n],
  );

  return { locale, setLocale };
}
