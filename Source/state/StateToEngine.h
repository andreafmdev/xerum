#pragma once

#include "engine/Arpeggiator.h"
#include "engine/ModMatrix.h"

#include <juce_data_structures/juce_data_structures.h>

namespace state
{
/**
 * Traduce il nodo MODS del ValueTree in uno snapshot. Message thread: qui si confrontano
 * stringhe, si scartano target e sorgenti sconosciute e si limita il depth. `out` viene
 * riscritto per intero, anche quando il nodo e' vuoto.
 *
 * Sta nel layer `state`, non in engine/: il motore non deve sapere che forma ha lo stato
 * salvato ne' includere juce_data_structures. Qui si conoscono entrambe le meta' — il formato
 * del ValueTree (StateTree.h) e le struct a dimensione fissa che il thread audio legge.
 */
void buildModSnapshot (const juce::ValueTree& modsNode, engine::ModSnapshot& out);

/**
 * Traduce il nodo ARP del ValueTree in uno snapshot. Message thread: qui si tokenizza una
 * stringa e si alloca. `out` viene riscritto per intero, anche quando il nodo e' assente o la
 * stringa e' piu' corta di kArpSteps (gli step mancanti restano a zero, come fa gia'
 * state::toVar verso la WebUI).
 */
void buildArpSnapshot (const juce::ValueTree& arpNode, engine::ArpSnapshot& out);
} // namespace state
