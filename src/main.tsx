import React from "react";
import ReactDOM from "react-dom/client";
import App from "./App";
import "./index.css";
import "./i18n";

declare const __MOONLIT_BUILD__: string | undefined;
try {
  console.debug(
    `[moonlit] frontend build: ${typeof __MOONLIT_BUILD__ !== "undefined" ? __MOONLIT_BUILD__ : "dev"}`,
  );
} catch {
  /* noop */
}

ReactDOM.createRoot(document.getElementById("root") as HTMLElement).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>,
);
