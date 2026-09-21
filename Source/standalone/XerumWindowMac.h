#pragma once

/**
 * Il poco di macOS che serve alla finestra dello Standalone. L'implementazione sta in
 * XerumWindowMac.mm — primo file Objective-C++ del progetto — e non compila altrove.
 *
 * `nsViewHandle` e' sempre cio' che ComponentPeer::getNativeHandle() restituisce su macOS:
 * un NSView*, da cui si risale alla finestra con [view window].
 */
namespace xerum
{
/** Barra del titolo trasparente e contenuto a tutta altezza: la finestra resta nativa —
    semaforo, full screen, Mission Control e snap continuano a funzionare — ma la barra
    sparisce alla vista e la UI arriva al bordo superiore.

    Idempotente, e oltre a mettere lo stile lo RIMETTE da sola: la prima chiamata registra un
    osservatore di NSWindowDidExitFullScreenNotification sulla finestra, perche' uscendo dal full
    screen JUCE riassegna lo styleMask e butta via FullSizeContentView (misurato: 0x800f -> 0xf).
    L'osservatore muore con la finestra, quindi non c'e' niente da disfare a mano. */
void makeWindowChromeless (void* nsViewHandle);

/** Aggiunge alla barra dei menu la voce View > Enter Full Screen (⌃⌘F, azione toggleFullScreen:).
    Senza di lei ⌃⌘F non fa NIENTE: su macOS quella scorciatoia non e' cablata nella finestra, e'
    il key equivalent di quella voce di menu, e il menu che JUCE costruisce per lo Standalone ha
    il solo menu dell'applicazione (misurato: mainMenu con 1 item, nessun toggleFullScreen:).
    Da chiamare una sola volta, all'avvio dell'app. */
void installFullScreenMenuItem();

/** Quanto spazio occupa il semaforo, in punti (pt) — AppKit misura i frame in punti, non in
    pixel: su uno schermo Retina i pixel sono il doppio. Misurato sui bottoni veri e non scritto
    a mano, perche' la larghezza esatta cambia con la versione di macOS. */
double trafficLightWidth (void* nsViewHandle);

/** Avvia il trascinamento nativo a partire dall'evento corrente. Falso se l'evento corrente
    non e' piu' il mousedown: vedi il Task 7 per cosa fare in quel caso. */
bool beginNativeWindowDrag (void* nsViewHandle);

/** Quello che fa il doppio clic sulla barra del titolo. */
void toggleWindowZoom (void* nsViewHandle);
} // namespace xerum
