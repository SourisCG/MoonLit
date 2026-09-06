import "./MoonlitLogo.css";

interface Props {
  size?: number;
}

/** MoonLit brand mark: red→blue crescent moon + white play (user's own logo). */
export function MoonlitLogo({ size = 28 }: Props) {
  const playScale = size / 28;
  return (
    <span
      className="moonlit-logo moonlit-logo-hover"
      aria-hidden
      style={{ width: size, height: size }}
    >
      <span
        className="moonlit-logo-play"
        style={{
          borderTopWidth: 4.5 * playScale,
          borderBottomWidth: 4.5 * playScale,
          borderLeftWidth: 7 * playScale,
        }}
      />
    </span>
  );
}
