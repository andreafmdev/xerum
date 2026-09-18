#pragma once

#include "dsp/MipTable.h"
#include "dsp/WavetableBlob.h"

#include <atomic>
#include <memory>
#include <vector>

namespace dsp
{
/** Costruisce la piramide band-limited di un blob. Alloca: mai dal thread audio. */
std::unique_ptr<MipTable> buildMipTable (const BlobView& blob);

/**
 * Possiede le tavole del plugin e ne pubblica una al thread audio.
 *
 * Le tavole costruite non vengono mai distrutte: è questo che rende sicuro il
 * puntatore atomico. Il thread audio può leggere `active()` mentre il message
 * thread ne costruisce un'altra, e nessuno gli toglie la memoria da sotto.
 * Costo massimo ≈ 1 MB per tavola.
 */
class WavetableStore
{
public:
    WavetableStore();

    /** Quante tavole conosce (una per opzione di `wtIndex`). */
    int getNumTables() const noexcept { return (int) tables_.size(); }

    /** Message thread / prepareToPlay: costruisce se serve e pubblica. Indice fuori range: nessun effetto. */
    void setActive (int index);

    /** Thread audio: la tavola pronta, o nullptr se non ce n'è ancora nessuna. */
    const MipTable* active() const noexcept { return active_.load (std::memory_order_acquire); }

private:
    std::vector<std::unique_ptr<MipTable>> tables_;
    std::atomic<const MipTable*> active_ { nullptr };
};
} // namespace dsp
