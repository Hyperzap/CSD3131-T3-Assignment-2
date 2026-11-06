#pragma once
#include "seam.h"

// Timing and cost totals collected while carving (feeds Part 5 tables).
struct CarveStats
{
    double energyMs = 0.0;  // time spent computing energy maps
    double findMs = 0.0;    // time spent inside the seam finder only
    double removeMs = 0.0;  // time spent removing seams
    double totalSeamCost = 0.0; // sum of seam costs (each on its own energy map)
    int seamsRemoved = 0;
};

// Removes one vertical seam: returns an image one column narrower.
Image removeVerticalSeam(const Image& img, const Seam& seam);

// Inverse of removeVerticalSeam: re-inserts the pixels (4 bytes each, RGBA,
// one per row) at seam[i]. Used for undo.
Image insertVerticalSeam(const Image& img, const Seam& seam, const std::vector<std::uint8_t>& pixels);

// The RGBA pixels on a vertical seam, one per row (what insertVerticalSeam needs).
std::vector<std::uint8_t> seamPixels(const Image& img, const Seam& seam);

// Removes k vertical seams with the given finder, recomputing energy each time.
// If seamOverlay is not null, it receives a copy of the input image with every
// removed seam drawn in red at its ORIGINAL position.
// incrementalEnergy: update the energy map around each removed seam instead of
// recomputing all of it (Part 5d). Same seams, much less energy work.
Image carveVertical(const Image& img, int k, const SeamFinder& finder,
                    CarveStats& stats, Image* seamOverlay = nullptr,
                    bool incrementalEnergy = false);

// Removes k horizontal seams: transpose, carve vertically, transpose back.
Image carveHorizontal(const Image& img, int k, const SeamFinder& finder,
                      CarveStats& stats, Image* seamOverlay = nullptr,
                      bool incrementalEnergy = false);
