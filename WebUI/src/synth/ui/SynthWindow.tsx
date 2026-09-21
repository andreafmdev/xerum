import { useCallback, useEffect, useLayoutEffect, useMemo, useRef, useState, type CSSProperties } from "react";
import { AnimatePresence } from "motion/react";
import { useBoolParam, useBridgeState } from "../../juce/hooks";
import { useBackend } from "../../juce/provider";
import type { ModSource } from "../../juce/backend";
import type { ParamId } from "../params.generated";
import { useSynth, type TabId } from "../useSynth";
import { BottomStrip } from "./BottomStrip";
import { consumeFirstBoot } from "./boot";
import { Header } from "./Header";
import { FilterPanel, MasterPanel, OscPanel } from "./Panels";
import { PresetOverlay } from "./PresetOverlay";
import { SettingsOverlay } from "./SettingsOverlay";
import { SynthContext, type SynthCtx } from "./SynthContext";
import { ArpTab, EnvTab, FxTab, LfoTab, ModTab, TabArea } from "./Tabs";
import { WaveDisplay } from "./WaveDisplay";
import "./synth.css";

/** Materiali dello chassis. `glass` (default) e `metal` sono i due del design "Versione glassy":
    vetro fumé sopra un'aurora in movimento, e metallo spazzolato con anelli OLED. */
export type SynthVariant = "deep" | "soft" | "glow" | "glass" | "metal";

export type SynthWindowProps = {
  /** Materiale del pannello. */
  variant?: SynthVariant;
  initialTab?: TabId;
  /** Scala fissa invece dell'adattamento al contenitore. */
  scale?: number;
  /** Margine totale (px) lasciato attorno allo chassis dall'adattamento.
      L'host JUCE passa 0: lo chassis riempie la WebView senza margine. */
  gutter?: number;
};

// La larghezza dello chassis, gemella di kChassisWidth in PluginEditor.cpp. Esportata per la
// stessa ragione di H: e' questo il numero, non il letterale nel C++, e SynthWindow.test.tsx
// rilegge il sorgente C++ per impedire che le due copie divergano in silenzio.
export const W = 900;
// 690 = l'altezza dello chassis nel design ("Synth Window", .sx-chassis { height: 690px }), e qui
// la somma dei figli la riempie esattamente — niente respiro in fondo e niente compressione.
//
// I figli sono tutti `shrink-0` e si sommano: padding verticale 20 + Header 40 + WaveDisplay 118
// + i tre pannelli 228 + TabArea 144 + BottomStrip 108 + quattro gap da 8 = 690.
//
// Da dove viene ogni numero: Header (40), riga dei pannelli (228) e gap (8) sono quelli del design.
// BottomStrip (108) sta al posto del Footer da 32 piu' la tastiera da 82 del prototipo, che sommati
// al loro gap farebbero 122: la striscia unica costa 14 px in meno perche' mette le rotelle di fianco
// ai tasti invece che i meter su una riga a parte.
//
// La TabArea resta 144 e NON scende ai 126 del design: la tab FX e' la sola a impilare
// un'intestazione sopra un knob dentro uno slot, e con i knob riportati alle misure del design
// (scatola `sm` da 46 px invece di 38) serve piu' spazio di prima, non meno. Misurato, non stimato.
//
// Il WaveDisplay prende quel che avanza: 690 - 20 - 32 - 40 - 228 - 144 - 108 = 118. Sono 20 px in
// meno dei 138 del design, ed e' esattamente il prestito che la tab FX aveva gia' chiesto quando
// l'altezza era 680; una forma d'onda li cede senza perdere niente, una griglia di controlli no.
//
// E' il numero da cui dipende tutta la geometria dell'editor: la regola .sx-chassis in synth.css
// deve restare uguale a questo valore (il test in SynthWindow.test.tsx controlla che non
// divergano), e anche kChassisHeight in PluginEditor.cpp. Esportata perche' e' quella verita',
// non il testo del CSS, a dover guidare chi la legge.
export const H = 690;

/** Il tetto della scala, gemello di kMaxScale in PluginEditor.cpp. */
export const MAX_SCALE = 1.5;

/** La scala con cui lo chassis riempie un contenitore di `width`×`height`, meno `gutter` in
    totale per lato. E' l'altra meta' della coppia con scaleForWidth() in PluginEditor.cpp, e le
    due NON sono la stessa formula: il C++ decide quanto grande puo' essere la FINESTRA (jlimit fra
    kMinScale=0.72 e kMaxScale=1.5, e da li' ricava l'altezza con ceil(H*scala)), questa decide
    come lo chassis riempie la WebView che quella finestra contiene.

    Il minimo del C++ non ha e non deve avere un gemello qui: sotto il minimo la finestra non ci
    va, e se ci andasse (browser in sviluppo, host che ignora il constrainer) rimpicciolire
    ancora e' la cosa giusta — un pavimento a 0.72 farebbe traboccare lo chassis fuori dalla
    WebView invece che lasciarlo rimpicciolire.

    Cio' che deve valere, e che il test in SynthWindow.test.tsx dimostra su tutto il dominio
    raggiungibile (larghezze 648..1350), e' questo: per ogni finestra che il ChassisConstrainer
    sa produrre, fitScale ritorna esattamente width/W e lo scarto verticale resta sotto il
    pixel — quello che avanza e' solo l'arrotondamento di ceil() nell'altezza della finestra.

    NB: la tesi opposta scritta in findings.md ("sotto i 648 px le due formule divergono e
    lasciano scoperte delle bande") e' stata verificata ed e' falsa due volte. Primo: il
    constrainer E' applicato alla finestra — simulando windowWillResize:toSize: sulla NSWindow
    viva, una proposta di 100x680 torna 648x490 e una di 1600x680 torna 1350x1020. Secondo: le
    misure di quella tabella (chassis 467x490 a viewport 648x490) non si riproducono — rimisurato
    con Chromium headless sul dev server, a 648x490 lo chassis e' 648x489.59, cioe' riempie. */
export function fitScale(width: number, height: number, gutter = 0) {
  return Math.min((width - gutter) / W, (height - gutter) / H, MAX_SCALE);
}

// Come si scala lo chassis: `zoom` se il motore lo implementa per bene, altrimenti `transform`.
//
// `transform: scale` rasterizza il sottoalbero alla dimensione di layout e poi stira il bitmap:
// sopra 1x testo e bordi si sfocano, ed e' la qualita' che si perde ingrandendo la finestra.
// `zoom` invece rifa' il layout alla scala nuova, quindi resta nitido a qualsiasi misura. Il
// problema e' che WebKit (il motore della WKWebView in cui gira il plugin) per anni lo ha
// implementato a modo suo: misurato con Playwright/WebKit a viewport 1309x873 e `zoom: 1.4544`
// sullo chassis, getBoundingClientRect restava 900x600 e i discendenti venivano *divisi* per
// il fattore invece che moltiplicati (una fascia vuota di ~270 px fra pannello e tastiera).
// Lo `zoom` standard (2024) e' arrivato dopo, e non si puo' sapere dalla versione quale dei
// due si ha davanti.
//
// Quindi non si sceglie a priori: si applica `zoom`, si misura lo chassis e, se la sua
// larghezza a schermo non e' 900 × scala, si ripiega su `transform`. La misura e' quella che
// la spec prescrive per lo zoom standard (getBoundingClientRect in pixel zoomati), quindi il
// WebKit vecchio fallisce il test e il nuovo lo passa.
//
// Il <canvas> di WaveDisplay ha comunque bisogno di conoscere la scala: ne' `transform` ne'
// `zoom` toccano il backing store, quindi lo schermo dell'onda resterebbe a risoluzione 1x
// anche quando tutto il resto e' ingrandito. La ricava da getBoundingClientRect / clientWidth
// (vedi WaveDisplay.tsx), che vale in entrambi i modi.
export type ScaleMode = "zoom" | "transform";

/** Finestra del plugin/** Finestra del plugin: 900×680 scalata per stare nel contenitore. Va montata dentro <BridgeProvider>. */
export function SynthWindow({ variant = "glass", initialTab = "env", scale: fixedScale, gutter = 16 }: SynthWindowProps) {
  const s = useSynth(initialTab);
  // L'host JUCE ricrea l'editor a ogni apertura della finestra: la sequenza piena va suonata
  // una volta sola per processo (consumeFirstBoot in boot.ts), non a ogni montaggio — un
  // useState con inizializzatore lazy chiama consumeFirstBoot() una sola volta, al primo
  // render di QUESTA istanza, e il risultato resta fisso per tutta la sua vita.
  const [boot] = useState<"first" | "again">(() => (consumeFirstBoot() ? "first" : "again"));
  // Unica istanza dello stato condiviso: i tab lo leggono dal contesto, così non
  // esistono copie che si aggiornano a turno con gli echo dell'host.
  const state = useBridgeState();
  // bypass cambia solo quando lo si preme: leggerlo qui non costa nulla. I parametri dell'onda
  // (wtpos/warp/level/wtIndex) invece cambiano a ogni pointermove e vivono dentro WaveDisplay,
  // altrimenti un drag sul knob ridisegnerebbe tutta la finestra, tastiera compresa.
  const bypass = useBoolParam("bypass");

  // Lo spazio per il semaforo (solo Standalone macOS, vedi backend.windowChrome): letto una
  // volta al mount, non a ogni render — WindowChannel non ha nessun ChangeListener, il semaforo
  // non cambia larghezza mentre l'app gira. 0 come valore iniziale: fuori dallo Standalone resta
  // cosi' per sempre, e anche dentro, finche' la risposta non arriva, l'header non lascia
  // nessun padding di troppo.
  const backend = useBackend();
  const [trafficLightWidth, setTrafficLightWidth] = useState(0);
  useEffect(() => {
    let cancelled = false;
    void backend.windowChrome().then(
      ({ trafficLightWidth }) => { if (!cancelled) setTrafficLightWidth(trafficLightWidth); },
      // Stessa convenzione dei fetch one-shot in juce/hooks.ts (getState, midiInputs,
      // audioSettings): senza il secondo argomento un rifiuto diventa una unhandled promise
      // rejection, non solo un padding mancato.
      (e) => console.warn("[bridge] windowChrome fallita", e),
    );
    return () => { cancelled = true; };
  }, [backend]);

  // useBridgeState ricrea addMod/setDepth/removeMod a ogni render: le avvolgiamo
  // dietro un ref per esporre callback stabili e non ricalcolare il contesto.
  const stateRef = useRef(state);
  stateRef.current = state;
  const { setTab, markDirty } = s;
  const addMod = useCallback(
    (src: ModSource, target: ParamId) => {
      stateRef.current.addMod(src, target);
      setTab("mod");
      markDirty();
    },
    [setTab, markDirty],
  );
  const setDepth = useCallback((i: number, depth: number) => { stateRef.current.setDepth(i, depth); markDirty(); }, [markDirty]);
  const removeMod = useCallback((i: number) => { stateRef.current.removeMod(i); markDirty(); }, [markDirty]);
  const setArpSteps = useCallback((steps: number[]) => { stateRef.current.setArpSteps(steps); markDirty(); }, [markDirty]);
  const ctx = useMemo<SynthCtx>(
    () => ({ mods: state.mods, arpSteps: state.arpSteps, addMod, setDepth, removeMod, setArpSteps, markDirty }),
    [state.mods, state.arpSteps, addMod, setDepth, removeMod, setArpSteps, markDirty],
  );

  const rootRef = useRef<HTMLDivElement>(null);
  const chassisRef = useRef<HTMLDivElement>(null);
  // L'ambient audio: un rAF coalescente scrive --m-out (l'unica variabile rimasta, vedi
  // conductor.ts) sull'host dell'aurora, non sullo chassis. Una custom property scritta su
  // chassisRef invaliderebbe lo stile dell'intero sottoalbero — Header, WaveDisplay, i tre
  // pannelli, TabArea, BottomStrip, tutto — trenta volte al secondo per un valore che un solo

  const [sc, setSc] = useState(fixedScale ?? 1);
  const [mode, setMode] = useState<ScaleMode>("zoom");
  // La verifica dello zoom, dopo il layout: una sola volta, alla prima scala diversa da 1.
  useLayoutEffect(() => {
    if (mode !== "zoom" || sc === 1) return;
    const el = chassisRef.current;
    if (!el) return;
    const width = el.getBoundingClientRect().width;
    if (!width) return; // jsdom e simili: nessun layout, niente da decidere
    if (Math.abs(width - W * sc) > 2) {
      if (import.meta.env.DEV) console.info(`[fit] zoom non standard (chassis ${width}px per scala ${sc}): uso transform`);
      setMode("transform");
    }
  }, [mode, sc]);
  useEffect(() => {
    if (fixedScale) {
      setSc(fixedScale);
      return;
    }
    const el = rootRef.current;
    if (!el) return;
    const fit = () => {
      const r = el.getBoundingClientRect();
      if (!r.width || !r.height) return;
      setSc(fitScale(r.width, r.height, gutter));
    };
    fit();
    const ro = new ResizeObserver(fit);
    ro.observe(el);
    return () => ro.disconnect();
  }, [fixedScale, gutter]);

  return (
    <div className="sx-root" ref={rootRef}>
      {/* data-attached non ha piu' nessun consumatore CSS: la regola .sx-chassis[data-attached] che
          squadrava gli angoli bassi per la striscia nativa e' sparita con la striscia. E non e'
          lui a togliere il margine — quello lo fa `gutter` dentro il calcolo del fit, qui sopra.
          Resta come segnale "questo chassis sta riempiendo una WebView", per chi dovesse volerlo
          leggere; un test lo blocca perche' non sparisca per distrazione. */}
      <div
        ref={chassisRef}
        data-testid="chassis"
        data-scale-mode={mode}
        className="sx-chassis"
        data-variant={variant}
        data-attached={gutter === 0 ? "" : undefined}
        data-boot={boot}
        // Il dimming del bypass passa da una custom property, non da `opacity` diretto: e' il
        // riposo che la keyframe di accensione in synth.css legge (var(--chassis-opacity)).
        // Un `opacity` inline qui vincerebbe la cascata ma perderebbe comunque contro
        // l'animazione mentre gira, e bypass e' stato di sessione/host — puo' essere gia'
        // attivo alla primissima apertura del processo, prima ancora che l'animazione parta.
        style={{
          ...(mode === "zoom" ? { zoom: sc } : { transform: `scale(${sc})` }),
          "--chassis-opacity": bypass.checked ? 0.9 : 1,
        } as unknown as CSSProperties}
      >
        {/* Host dedicato dell'aurora (solo variante glass, vedi synth.css): un nodo vuoto, non
            lo chassis, cosi' --m-out invalida lo stile di questo div e dei suoi due
            pseudo-elementi, non quello di tutto cio' che segue. */}
        <div className="sx-aurora" aria-hidden="true" />
        <SynthContext value={ctx}>
          <Header
            preset={s.preset}
            dirty={s.dirty}
            onBrowse={() => s.setBrowse(true)}
            onPrev={() => s.stepPreset(-1)}
            onNext={() => s.stepPreset(1)}
            onSettings={() => s.setSettings(true)}
            scale={sc}
            trafficLightWidth={trafficLightWidth}
          />
          <WaveDisplay scale={sc} />
          <div className="flex h-57 shrink-0 gap-2">
            <OscPanel />
            <FilterPanel />
            <MasterPanel />
          </div>
          <TabArea tab={s.tab} setTab={s.setTab}>
            {s.tab === "env" && <EnvTab />}
            {s.tab === "lfo" && <LfoTab />}
            {s.tab === "mod" && <ModTab />}
            {s.tab === "fx" && <FxTab />}
            {s.tab === "arp" && <ArpTab />}
          </TabArea>
          <BottomStrip />
          <AnimatePresence>
            {s.browse && <PresetOverlay current={s.preset} onPick={s.pick} onClose={() => s.setBrowse(false)} />}
            {s.settings && <SettingsOverlay onClose={() => s.setSettings(false)} />}
          </AnimatePresence>
        </SynthContext>
      </div>
    </div>
  );
}
