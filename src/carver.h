#pragma once
// Stateful, step-by-step seam carver: the engine behind both the desktop
// viewer (seamviewer) and the web build (seam.js / seam.wasm).
//
// Each seam takes two steps:
//   1. findSeam():   the next seam is computed and becomes "pending" (highlighted)
//   2. removeSeam(): the pending seam is removed; the image shrinks by one pixel
// stepBack() undoes those in reverse, re-inserting the removed pixels exactly.
// Steps already computed are cached, so going back and forward again is instant.
// The energy map is kept up to date incrementally (Part 5d).

#include "carve.h"

#include <string>
#include <vector>

enum class Algo
{
    DP = 0,
    Greedy,
    GraphCut,
    Count
};

const char* algoName(Algo a);          // "DP", "Greedy", "Graph Cut"
const char* algoId(Algo a);            // "dp", "greedy", "graphcut"
bool parseAlgo(const std::string& s, Algo& out); // accepts the ids above
SeamFinder finderFor(Algo a);

struct CarveStep
{
    Seam seam;                 // seam in the coordinates of the image at that step:
                               //   vertical:   seam[row] = column
                               //   horizontal: seam[column] = row
    bool horizontal = false;
    Algo algo = Algo::DP;
    double cost = 0.0;         // sum of energies along the seam
    double findMs = 0.0;       // time spent in the seam finder
    std::vector<std::uint8_t> removed; // RGBA of the removed pixels (filled on removal)
};

class Carver
{
public:
    explicit Carver(Image img);

    // Back to the original image; clears all history.
    void reset();

    const Image& original() const { return original_; }
    const Image& image() const { return current_; }
    const EnergyMap& energy(); // energy map of the current image

    // Phase 1: compute the next seam and make it pending. If a pending or cached
    // step already exists with the same settings it is reused; with different
    // settings the cached future is discarded first.
    // Returns false (see lastError()) if the image is too small.
    bool findSeam(Algo algo, bool horizontal);

    // Phase 2: remove the pending seam. Returns false if there is none.
    bool removeSeam();

    // Convenience: removeSeam() if a seam is pending, otherwise findSeam().
    bool stepForward(Algo algo, bool horizontal);

    // Undo one phase: un-highlight the pending seam, or re-insert the last
    // removed seam (which becomes pending again). False at the original image.
    bool stepBack();

    // Drops the pending seam and all cached redo steps.
    void discardFuture();

    bool hasPending() const { return pending_; }
    const CarveStep* pending() const { return pending_ ? &history_[applied_] : nullptr; }

    // Removed steps, oldest first ([0, stepsRemoved())).
    int stepsRemoved() const { return applied_; }
    const CarveStep& step(int k) const { return history_[k]; }

    // True if findSeam(algo, horizontal) can reuse a cached result (no computation).
    bool hasCachedNext(Algo algo, bool horizontal) const;

    const std::string& lastError() const { return error_; }

private:
    Image original_;
    Image current_;
    std::vector<CarveStep> history_; // [0, applied_) removed; [applied_, end) cached
    int applied_ = 0;
    bool pending_ = false;

    EnergyMap energy_;
    bool energyValid_ = false;
    std::string error_;
};
