import { describe, expect, it } from "vitest";
import { fireEvent, render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { Header } from "./Header";
import { PRESETS } from "../presets";
import { FakeBackend } from "../../juce/fake-backend";
import { BridgeProvider } from "../../juce/provider";
import { SynthContext, type SynthCtx } from "./SynthContext";

type Extra = { chrome?: { trafficLightWidth: number }; scale?: number };

// Header usa useDirty() per il bottone Bypass, che richiede <SynthContext>: il brief non lo
// mette nell'helper di test (ne' PerformanceBar.test.tsx, il modello, ne ha bisogno perché
// PerformanceBar non tocca il preset), ma senza di lui il render lancia "SynthContext mancante".
const noopCtx: SynthCtx = { mods: [], arpSteps: [], addMod: () => {}, setDepth: () => {}, removeMod: () => {}, setArpSteps: () => {}, markDirty: () => {} };

const headerWith = (backend: FakeBackend, extra: Extra = {}) => (
  <BridgeProvider backend={backend}>
    <SynthContext value={noopCtx}>
      <Header
        preset={PRESETS[0]!}
        dirty={false}
        onPrev={() => {}}
        onNext={() => {}}
        onBrowse={() => {}}
        onSettings={() => {}}
        scale={extra.scale ?? 1}
        trafficLightWidth={extra.chrome?.trafficLightWidth ?? 0}
      />
    </SynthContext>
  </BridgeProvider>
);

const renderHeader = (backend: FakeBackend, extra: Extra = {}) => render(headerWith(backend, extra));

describe("Header", () => {
  it("il mousedown sull'header trascina la finestra", async () => {
    const backend = new FakeBackend();
    renderHeader(backend);
    await userEvent.pointer({ target: screen.getByRole("banner"), keys: "[MouseLeft>]" });
    expect(backend.windowDrags).toBe(1);
  });

  it("il mousedown su un controllo NON trascina la finestra", async () => {
    // Senza questa esclusione la finestra si sposterebbe mentre giri una manopola: l'errore
    // che renderebbe la UI inusabile.
    const backend = new FakeBackend();
    renderHeader(backend);
    await userEvent.pointer({ target: screen.getByLabelText("Settings"), keys: "[MouseLeft>]" });
    expect(backend.windowDrags).toBe(0);
  });

  it("lo spazio per il semaforo c'è solo in Standalone ed è diviso per lo scale", () => {
    // Il semaforo lo disegna macOS in coordinate di finestra; l'header vive dentro lo chassis
    // scalato. A scala 1.5 un padding di 78 px CSS ne occuperebbe 117 sulla finestra.
    const { rerender } = renderHeader(new FakeBackend(), { chrome: { trafficLightWidth: 78 }, scale: 1.5 });
    expect(screen.getByRole("banner")).toHaveStyle({ paddingLeft: "52px" });
    rerender(headerWith(new FakeBackend(), { chrome: { trafficLightWidth: 0 }, scale: 1.5 }));
    expect(screen.getByRole("banner")).toHaveStyle({ paddingLeft: "0px" });
  });

  it("se il trascinamento nativo non parte, il mousemove che segue chiama moveWindowBy", async () => {
    // Il ripiego non è codice morto: è la strada presa su ogni versione di macOS dove
    // l'evento del mousedown non è più valido quando il messaggio della bridge arriva.
    //
    // fireEvent con screenX/screenY espliciti, non userEvent.pointer({ coords: { clientX } }):
    // Header.tsx legge screenX/screenY (I1), e userEvent.pointer non li valorizza (restano 0),
    // quindi userEvent.pointer non farebbe scattare nessun moveWindowBy qui.
    const backend = new FakeBackend();
    backend.nextDragStarts = false;
    renderHeader(backend);
    const header = screen.getByRole("banner");
    fireEvent.mouseDown(header, { screenX: 100, screenY: 100, clientX: 100, clientY: 100, buttons: 1 });
    // beginWindowDrag() e' async: il .then() dentro Header che attacca il listener mousemove
    // gira solo dopo che la sua promise si e' risolta, un macrotask dopo quella interna del
    // finto backend (vedi il commento identico nel test del rilascio anticipato piu' sotto).
    // Senza questa attesa il mousemove sotto arriva prima che il listener sia attaccato e va
    // perso.
    await new Promise((r) => setTimeout(r, 0));
    fireEvent.mouseMove(window, { screenX: 130, screenY: 84, clientX: 130, clientY: 84, buttons: 1 });
    await waitFor(() => expect(backend.moves.length).toBeGreaterThan(0));
    expect(backend.moves[0]).toEqual([30, -16]);
  });

  it("il ripiego usa screenX/screenY: il secondo delta resta pieno anche dopo che la finestra si e' mossa (I1)", async () => {
    // Il test sopra asserisce solo il PRIMO delta, che e' corretto sia con clientX sia con
    // screenX: la finestra non si e' ancora mossa quando quel delta viene calcolato, quindi le
    // due coordinate coincidono. Il bug I1 comincia dal secondo evento: clientX/clientY sono
    // relative alla viewport, e la viewport E' la finestra, quindi ogni moveWindowBy riuscito
    // trasla l'origine da cui sono misurate esattamente della stessa quantita' e il delta
    // successivo calcolato da clientX si annulla (o si distorce). screenX/screenY sono assolute
    // e non risentono del movimento della finestra.
    //
    // Per vederlo, il finto backend deve comportarsi come una finestra vera: dopo la prima
    // moveWindowBy, i clientX/clientY che il sistema offrirebbe al prossimo evento sono quelli
    // "grezzi" (come se il cursore si fosse mosso in coordinate schermo) MENO lo spostamento che
    // la finestra ha appena subito — mentre screenX/screenY restano quelli assoluti del cursore.
    // Con screenX il secondo delta e' pieno (10,10); con clientX sarebbe tutt'altro, perche' lo
    // spostamento della finestra (30,-16) si mescola nel calcolo.
    const backend = new FakeBackend();
    backend.nextDragStarts = false;
    renderHeader(backend);
    const header = screen.getByRole("banner");

    fireEvent.mouseDown(header, { screenX: 100, screenY: 100, clientX: 100, clientY: 100, buttons: 1 });
    // beginWindowDrag() e' async: senza aspettare un macrotask il .then() che attacca il
    // listener mousemove non e' ancora girato (stesso motivo del test del rilascio anticipato).
    await new Promise((r) => setTimeout(r, 0));

    // Primo passo: cursore a (130, 84) in coordinate schermo. La finestra non si e' ancora
    // mossa, quindi clientX/clientY coincidono con screenX/screenY.
    fireEvent.mouseMove(window, { screenX: 130, screenY: 84, clientX: 130, clientY: 84, buttons: 1 });
    await waitFor(() => expect(backend.moves).toHaveLength(1));
    expect(backend.moves[0]).toEqual([30, -16]);

    // Secondo passo: il cursore continua per altri (10, 10) in coordinate schermo (assolute).
    // Nel frattempo la finestra finta si e' spostata di backend.windowMoved (il totale delle
    // moveWindowBy chieste finora): una finestra vera rifletterebbe quello spostamento nel
    // clientX/clientY del prossimo evento, quindi lo si simula qui sottraendo l'accumulato dal
    // clientX/clientY "grezzo", mentre screenX/screenY restano assoluti.
    const [movedX, movedY] = backend.windowMoved;
    fireEvent.mouseMove(window, {
      screenX: 140, screenY: 94,
      clientX: 140 - movedX, clientY: 94 - movedY,
      buttons: 1,
    });
    await waitFor(() => expect(backend.moves).toHaveLength(2));
    expect(backend.moves[1]).toEqual([10, 10]);
  });

  it("se il trascinamento nativo parte, il mousemove che segue NON chiama moveWindowBy", async () => {
    const backend = new FakeBackend();
    renderHeader(backend);
    const header = screen.getByRole("banner");
    await userEvent.pointer([
      { target: header, coords: { clientX: 100, clientY: 100 }, keys: "[MouseLeft>]" },
      { target: header, coords: { clientX: 130, clientY: 84 } },
    ]);
    expect(backend.moves).toHaveLength(0);
  });

  it("un rilascio arrivato prima della risposta di beginWindowDrag non arma il ripiego", async () => {
    // WKWebView consegna i messaggi della bridge in modo asincrono: il round-trip di
    // beginWindowDrag() puo' rispondere quando il bottone del mouse e' gia' stato rilasciato.
    // Senza il fix, il "false" tardivo armava comunque i listener del ripiego, che poi
    // seguivano il cursore a bottone alzato finche' un mouseup qualsiasi, altrove, non li
    // fermava per caso.
    const backend = new FakeBackend();
    const resolveDrag = backend.armPendingDrag();
    renderHeader(backend);
    const header = screen.getByRole("banner");

    fireEvent.mouseDown(header, { clientX: 100, clientY: 100, buttons: 1 });
    fireEvent.mouseUp(window, { clientX: 100, clientY: 100, buttons: 0 });

    // La risposta tardiva: il drag nativo non e' partito.
    resolveDrag(false);
    // beginWindowDrag() e' una funzione async che ritorna una promise sospesa: la sua stessa
    // promise si risolve un giro dopo quella interna, quindi un macrotask (non un microtask
    // solo) garantisce che il .then() dentro Header sia gia' girato.
    await new Promise((r) => setTimeout(r, 0));

    // Un mousemove qualsiasi, a bottone rilasciato, non deve muovere la finestra.
    fireEvent.mouseMove(window, { clientX: 200, clientY: 200, buttons: 0 });
    expect(backend.moves).toHaveLength(0);
  });

  it("il doppio clic sull'header attiva lo zoom nativo", async () => {
    const backend = new FakeBackend();
    renderHeader(backend);
    await userEvent.dblClick(screen.getByRole("banner"));
    expect(backend.zoomToggles).toBe(1);
  });

  it("il doppio clic su un controllo NON attiva lo zoom nativo", async () => {
    const backend = new FakeBackend();
    renderHeader(backend);
    await userEvent.dblClick(screen.getByLabelText("Settings"));
    expect(backend.zoomToggles).toBe(0);
  });
});
