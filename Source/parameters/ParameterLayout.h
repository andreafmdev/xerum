#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace params
{
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/**
 * Riporta ogni parametro dell'APVTS al suo default dichiarato in parameters.json.
 *
 * Serve prima di replaceState(): un parametro *assente* dallo stato salvato non riceve nessuna
 * notifica quando l'albero viene sostituito (JUCE cicla sui figli del file, non sullo schema, e
 * per l'adapter rimasto senza nodo si limita a ricrearne uno col valore corrente), quindi si
 * porterebbe dietro il valore del preset precedente invece di tornare al default. Azzerando
 * prima, il verso dell'iterazione diventa quello giusto — si cicla sullo schema corrente e si
 * pesca dal file — e da' gratis le tre regole che servono: parametro aggiunto dopo il salvataggio
 * -> default, parametro rimosso dallo schema -> ignorato, nessun residuo del suono precedente.
 *
 * E' la stessa cosa che StateChannel::applyPreset fa gia' per i preset interni, dove il default
 * arriva da params::presetValue() per i parametri che il preset non elenca.
 */
void resetToDefaults (juce::AudioProcessorValueTreeState& apvts);
} // namespace params
