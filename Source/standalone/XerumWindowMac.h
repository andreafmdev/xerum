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
    sparisce alla vista e la UI arriva al bordo superiore. */
void makeWindowChromeless (void* nsViewHandle);

/** Quanto spazio occupa il semaforo, in px di finestra, misurato sui bottoni veri e non
    scritto a mano: la larghezza esatta cambia con la versione di macOS. */
double trafficLightWidth (void* nsViewHandle);

/** Avvia il trascinamento nativo a partire dall'evento corrente. Falso se l'evento corrente
    non e' piu' il mousedown: vedi il Task 7 per cosa fare in quel caso. */
bool beginNativeWindowDrag (void* nsViewHandle);

/** Quello che fa il doppio clic sulla barra del titolo. */
void toggleWindowZoom (void* nsViewHandle);
} // namespace xerum
