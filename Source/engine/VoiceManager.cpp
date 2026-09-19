#include "engine/VoiceManager.h"

namespace engine
{
void VoiceManager::prepare (double sampleRate) noexcept
{
    for (auto& voice : voices_)
        voice.prepare (sampleRate);

    nextStartOrder_ = 1;
}

void VoiceManager::reset() noexcept
{
    for (auto& voice : voices_)
        voice.reset();

    nextStartOrder_ = 1;
}

SynthVoice* VoiceManager::findFreeVoice() noexcept
{
    for (auto& voice : voices_)
        if (! voice.isActive())
            return &voice;

    return nullptr;
}

SynthVoice* VoiceManager::findVoiceForNote (int midiNote) noexcept
{
    // Le voci in dissolvenza da furto sono escluse, e non e' un dettaglio: conservano il numero
    // di nota che avevano, quindi senza questo filtro una coda potrebbe essere ribattuta da
    // retrigger() (l'inviluppo ripartirebbe mentre la dissolvenza continua a scendere) o
    // fermata da un note-off destinato alla voce che l'ha sostituita.
    for (auto& voice : voices_)
        if (voice.isActive() && ! voice.isFading() && voice.getMidiNote() == midiNote)
            return &voice;

    return nullptr;
}

int VoiceManager::countActive() const noexcept
{
    int count = 0;

    for (const auto& voice : voices_)
        if (voice.isActive() && ! voice.isFading())
            ++count;

    return count;
}

int VoiceManager::countSounding() const noexcept
{
    int count = 0;

    for (const auto& voice : voices_)
        if (voice.isActive())
            ++count;

    return count;
}

const SynthVoice& VoiceManager::getVoice (int index) const noexcept
{
    const auto clamped = index >= 0 && index < poolSize ? index : 0;
    return voices_[static_cast<size_t> (clamped)];
}

SynthVoice* VoiceManager::chooseVictim() noexcept
{
    SynthVoice* released = nullptr;
    SynthVoice* oldest = nullptr;

    for (auto& voice : voices_)
    {
        if (! voice.isActive() || voice.isFading())
            continue;

        if (oldest == nullptr || voice.getStartOrder() < oldest->getStartOrder())
            oldest = &voice;

        if (! voice.isReleasing())
            continue;

        // A parita' di livello vince la piu' vecchia: il confronto stretto sul livello lascia
        // in piedi la prima trovata, e il secondo ramo la sostituisce solo se e' partita prima.
        // Serve davvero, e il caso non e' esotico: con sustain a zero un accordo rilasciato ha
        // tutte le voci esattamente a livello zero, e senza questo l'ordine del pool deciderebbe
        // al posto del criterio.
        if (released == nullptr
            || voice.getAmplitudeLevel() < released->getAmplitudeLevel()
            || (voice.getAmplitudeLevel() == released->getAmplitudeLevel()
                && voice.getStartOrder() < released->getStartOrder()))
            released = &voice;
    }

    return released != nullptr ? released : oldest;
}

SynthVoice* VoiceManager::reclaimQuietestFading() noexcept
{
    SynthVoice* quietest = nullptr;

    for (auto& voice : voices_)
    {
        if (! voice.isActive() || ! voice.isFading())
            continue;

        if (quietest == nullptr || voice.getAmplitudeLevel() < quietest->getAmplitudeLevel())
            quietest = &voice;
    }

    if (quietest != nullptr)
        quietest->kill();

    return quietest;
}

void VoiceManager::noteOn (int midiNote, float velocity) noexcept
{
    // Nota ribattuta mentre suona ancora: si riparte sulla stessa voce invece di ucciderla e
    // riprenderla da capo. kill() azzerava inviluppo, fase e filtro in un colpo, cioe' un
    // gradino da ampiezza piena a zero fra due campioni: il clic che si sentiva a ogni nota
    // ripetuta, anche legato.
    if (auto* existing = findVoiceForNote (midiNote))
    {
        existing->retrigger (velocity);
        return;
    }

    // Il furto scatta sulla **polifonia** piena, non sul pool pieno: gli slot di margine non
    // sono voci in piu' da suonare, sono il posto dove le code dei furti finiscono di spegnersi.
    // Confondere i due numeri qui vorrebbe dire una polifonia di poolSize e nessun margine.
    if (countActive() >= maxVoices)
        if (auto* victim = chooseVictim())
            victim->beginStealFade();

    // E questa e' l'altra meta': la nota nuova prende una voce **davvero libera**, non quella
    // appena rubata. La voce rubata resta dov'e' a dissolvere, e nessuno la azzera fra due
    // campioni adiacenti.
    SynthVoice* voice = findFreeVoice();

    if (voice == nullptr)
        voice = reclaimQuietestFading();

    if (voice == nullptr)
        return; // irraggiungibile: countActive() <= maxVoices e il pool ne ha stealFadeMargin in piu'

    voice->start (midiNote, velocity, nextStartOrder_++);
}

void VoiceManager::noteOff (int midiNote) noexcept
{
    if (auto* voice = findVoiceForNote (midiNote))
        voice->stop();
}

void VoiceManager::allNotesOff() noexcept
{
    for (auto& voice : voices_)
        voice.stop();
}

void VoiceManager::allSoundOff() noexcept
{
    for (auto& voice : voices_)
        voice.kill();
}

void VoiceManager::render (float* outL, float* outR, int numSamples) noexcept
{
    for (auto& voice : voices_)
        voice.render (outL, outR, numSamples);
}

void VoiceManager::setWavetable (const dsp::MipTable* table) noexcept
{
    for (auto& voice : voices_)
        voice.setWavetable (table);
}

void VoiceManager::setParams (const EngineParams& p) noexcept
{
    for (auto& voice : voices_)
        voice.setParams (p);
}

void VoiceManager::setGlobalLfoLevel (float level) noexcept
{
    for (auto& voice : voices_)
        voice.setGlobalLfoLevel (level);
}

VoiceManager::SourceLevels VoiceManager::getSourceLevels() const noexcept
{
    // Le code dei furti sono saltate: mostrano l'inviluppo di una nota che l'esecutore ha gia'
    // smesso di suonare, e per un meter sarebbe un valore che non corrisponde a niente di
    // premuto. "La prima voce attiva" resta la regola, solo fra quelle che contano.
    for (const auto& voice : voices_)
        if (voice.isActive() && ! voice.isFading())
            return { voice.getSourceLevel (ModSource::lfo),
                     voice.getSourceLevel (ModSource::env),
                     voice.getSourceLevel (ModSource::env2),
                     voice.getSourceLevel (ModSource::vel) };

    return {};
}
} // namespace engine
