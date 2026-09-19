#pragma once

namespace params
{
/**
 * Identifica un parametro grezzo senza passare per il suo nome: chi implementa l'accessore
 * risolve `id -> puntatore` una volta sola alla costruzione (vedi PluginProcessor::paramSlots_),
 * e qui dentro e' solo un indice di array. Ordine arbitrario ma stabile: e' un dettaglio interno
 * fra questo header e chi scrive l'accessore, non un ABI pubblico.
 *
 * Vive in un header proprio, separato da ParamCollect.h, perche' engine/ModMatrix.h ha bisogno
 * dell'enum e non del template collectEngineParams con tutte le sue dipendenze.
 */
enum class ParamSlot : int
{
    oscOn, wtpos, oct, semi, fine, level, filtOn, ftype, slope, cutoff,
    res, drive, keytrk, att, dec, sus, rel, envVel, pan, bypass,
    lshape, lrate, lsync, lphase, lfade, lretrig,
    unison, detune,
    count
};
} // namespace params
