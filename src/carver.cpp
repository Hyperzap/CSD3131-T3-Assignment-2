#include "carver.h"

#include <chrono>
#include <utility>

const char* algoName(Algo a)
{
    switch (a)
    {
    case Algo::Greedy: return "Greedy";
    case Algo::GraphCut: return "Graph Cut";
    default: return "DP";
    }
}

const char* algoId(Algo a)
{
    switch (a)
    {
    case Algo::Greedy: return "greedy";
    case Algo::GraphCut: return "graphcut";
    default: return "dp";
    }
}

bool parseAlgo(const std::string& s, Algo& out)
{
    for (int k = 0; k < static_cast<int>(Algo::Count); ++k)
    {
        if (s == algoId(static_cast<Algo>(k)))
        {
            out = static_cast<Algo>(k);
            return true;
        }
    }
    return false;
}

SeamFinder finderFor(Algo a)
{
    switch (a)
    {
    case Algo::Greedy: return findVerticalSeamGreedy;
    case Algo::GraphCut: return findVerticalSeamGraphCut;
    default: return findVerticalSeamDPCompact; // same seams as findVerticalSeamDP, less memory
    }
}

Carver::Carver(Image img) : original_(std::move(img))
{
    reset();
}

void Carver::reset()
{
    current_ = original_;
    history_.clear();
    applied_ = 0;
    pending_ = false;
    energyValid_ = false;
    error_.clear();
}

const EnergyMap& Carver::energy()
{
    if (!energyValid_)
    {
        energy_ = computeEnergy(current_);
        energyValid_ = true;
    }
    return energy_;
}

bool Carver::hasCachedNext(Algo algo, bool horizontal) const
{
    return static_cast<int>(history_.size()) > applied_ && history_[applied_].algo == algo &&
           history_[applied_].horizontal == horizontal;
}

void Carver::discardFuture()
{
    history_.resize(applied_);
    pending_ = false;
}

bool Carver::findSeam(Algo algo, bool horizontal)
{
    error_.clear();

    if (hasCachedNext(algo, horizontal))
    {
        pending_ = true;
        return true;
    }
    discardFuture(); // cached steps were made with other settings

    const int len = horizontal ? current_.height : current_.width;
    if (len <= 2)
    {
        error_ = std::string("Image is too small to remove another ") + (horizontal ? "row." : "column.");
        return false;
    }

    CarveStep s;
    s.algo = algo;
    s.horizontal = horizontal;

    // Horizontal seams = vertical seams of the transposed image. The energy of the
    // transposed image is the transposed energy map, so no recomputation is needed.
    const EnergyMap e = horizontal ? transposeEnergy(energy()) : energy();

    const auto t0 = std::chrono::steady_clock::now();
    s.seam = finderFor(algo)(e);
    s.findMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    s.cost = seamCost(e, s.seam);

    history_.push_back(std::move(s));
    pending_ = true;
    return true;
}

bool Carver::removeSeam()
{
    error_.clear();
    if (!pending_)
    {
        error_ = "No seam is pending.";
        return false;
    }

    CarveStep& s = history_[applied_];
    if (s.horizontal)
    {
        const Image t = transposeImage(current_);
        s.removed = seamPixels(t, s.seam);
        const Image tOut = removeVerticalSeam(t, s.seam);
        if (energyValid_)
        {
            EnergyMap et = transposeEnergy(energy_);
            updateEnergyAfterSeamRemoval(et, tOut, s.seam);
            energy_ = transposeEnergy(et);
        }
        current_ = transposeImage(tOut);
    }
    else
    {
        s.removed = seamPixels(current_, s.seam);
        current_ = removeVerticalSeam(current_, s.seam);
        if (energyValid_)
            updateEnergyAfterSeamRemoval(energy_, current_, s.seam);
    }

    ++applied_;
    pending_ = false;
    return true;
}

bool Carver::stepForward(Algo algo, bool horizontal)
{
    if (pending_)
    {
        // A pending seam made with other settings is replaced, not removed.
        const CarveStep& p = history_[applied_];
        if (p.algo == algo && p.horizontal == horizontal)
            return removeSeam();
    }
    return findSeam(algo, horizontal);
}

bool Carver::stepBack()
{
    error_.clear();
    if (pending_)
    {
        pending_ = false; // un-highlight; the seam stays cached for redo
        return true;
    }
    if (applied_ == 0)
    {
        error_ = "Already at the original image.";
        return false;
    }

    --applied_;
    const CarveStep& s = history_[applied_];
    if (s.horizontal)
        current_ = transposeImage(insertVerticalSeam(transposeImage(current_), s.seam, s.removed));
    else
        current_ = insertVerticalSeam(current_, s.seam, s.removed);

    energyValid_ = false; // recomputed on demand
    pending_ = true;
    return true;
}
