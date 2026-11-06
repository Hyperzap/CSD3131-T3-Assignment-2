#pragma once
// Shared types for the seam carving assignment.
// Every seam finder takes an EnergyMap and returns a Seam, so DP / Greedy /
// Graph Cut all plug into the same carving loop (see carve.h).
//
// Nothing in the core depends on OpenCV (see image.h).

#include "image.h"

#include <functional>
#include <string>
#include <vector>

// Flat row-major energy grid: e(i, j) = data[i * cols + j].
struct EnergyMap
{
    int rows = 0;
    int cols = 0;
    std::vector<double> data;

    double operator()(int i, int j) const { return data[static_cast<size_t>(i) * cols + j]; }
    double& operator()(int i, int j) { return data[static_cast<size_t>(i) * cols + j]; }
};

// A vertical seam: seam[i] = column of the pixel removed in row i.
// (Horizontal seams are found by transposing the image, so they use the same type.)
using Seam = std::vector<int>;

// A seam finder returns a vertical seam for the given energy map.
using SeamFinder = std::function<Seam(const EnergyMap&)>;

// ---- energy.cpp ----
// Energy = |dI/dx| + |dI/dy| with 3x3 Sobel kernels and replicated borders,
// summed over the R, G, B channels. Integer arithmetic: identical to
// cv::Sobel(CV_32F, BORDER_REPLICATE), which the first version of this code used.
EnergyMap computeEnergy(const Image& img);

// Normalised grayscale visualisation of an energy map (RGBA, for display/reports).
Image energyToImage(const EnergyMap& e);

// Swaps rows and columns. Energy is symmetric under transposition
// (computeEnergy(transpose(I)) == transpose(computeEnergy(I))), which lets
// horizontal seams reuse the vertical-seam energy map.
EnergyMap transposeEnergy(const EnergyMap& e);

// Sum of e(i, seam[i]) over all rows: the "disruption measure" of the seam.
double seamCost(const EnergyMap& e, const Seam& seam);

// Part 5d: after removing `seam` (vertical) from the image, update the energy
// map in place instead of recomputing it: shift it like the image, then
// recompute only the ~3 pixels per row next to the seam. Gives exactly the
// same result as computeEnergy(newImg).
void updateEnergyAfterSeamRemoval(EnergyMap& e, const Image& newImg, const Seam& seam);

// ---- seam_dp.cpp ----
// Optimal vertical seam via dynamic programming:
//   M[0][j] = e[0][j]
//   M[i][j] = e[i][j] + min(M[i-1][j-1], M[i-1][j], M[i-1][j+1])
// then backtrack from argmin_j M[m-1][j].  Time O(mn), space O(mn).
Seam findVerticalSeamDP(const EnergyMap& e);

// ---- seam_dp_variants.cpp (Part 5d improvements, same seams as above) ----
// Two rolling cost rows + int8 backtrack offsets: ~12x less memory.
Seam findVerticalSeamDPCompact(const EnergyMap& e);
// Compact DP with each row split across threads (OpenMP). threads <= 0: all cores.
Seam findVerticalSeamDPParallel(const EnergyMap& e, int threads);
// Number of threads OpenMP would use (1 if built without OpenMP).
int maxThreads();

// ---- seam_greedy.cpp ----
// Greedy: start at the minimum-energy pixel of row 0, then in each next row
// step to the cheapest of the (up to) 3 neighbours below.  Time O(m + n).
Seam findVerticalSeamGreedy(const EnergyMap& e);

// ---- seam_graphcut.cpp ----
// Min s-t cut / max-flow formulation (Part 4), solved with Dinic's algorithm.
Seam findVerticalSeamGraphCut(const EnergyMap& e);
// Same, but also reports the max-flow value (= min cut = seam cost).
Seam findVerticalSeamGraphCutEx(const EnergyMap& e, double* maxFlowOut);
