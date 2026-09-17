import { useEffect, useState } from "react";

export default function App() {
  const [ready, setReady] = useState(false);

  useEffect(() => {
    setReady(true);
  }, []);

  return (
    <main className="shell">
      <header className="hero">
        <p className="eyebrow">SerumStyleSynth · Phase 1</p>
        <h1>Wavetable synth scaffold</h1>
        <p className="lede">
          React UI placeholder. Parameter bridge and wavetable editor land in later
          phases. Audio runs in C++ (<code>SynthEngine</code>).
        </p>
      </header>

      <section className="panel" aria-label="Status">
        <div className="row">
          <span>WebView</span>
          <strong>{ready ? "connected" : "booting…"}</strong>
        </div>
        <div className="row">
          <span>JUCE bridge</span>
          <strong>stub</strong>
        </div>
        <div className="row">
          <span>Engine</span>
          <strong>MIDI → voices → silence</strong>
        </div>
      </section>
    </main>
  );
}
