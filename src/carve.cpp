#include "carve.h"

#include <algorithm>
#include <chrono>

namespace
{
using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// Removes one element per row (at seam[i]) from a row-major grid whose cells
// are `cellSize` values wide. Copies each row in two chunks: left of the seam,
// then right of it. Used for the image (4 bytes per pixel) and the
// original-column map (1 int per pixel).
template <typename T>
std::vector<T> removeSeamFromGrid(const std::vector<T>& src, int rows, int cols, int cellSize, const Seam& seam)
{
    const size_t inRow = static_cast<size_t>(cols) * cellSize;
    const size_t outRow = inRow - cellSize;
    std::vector<T> dst(static_cast<size_t>(rows) * outRow);
    for (int i = 0; i < rows; ++i)
    {
        const T* s = &src[i * inRow];
        T* d = &dst[i * outRow];
        const size_t cut = static_cast<size_t>(seam[i]) * cellSize;
        std::copy(s, s + cut, d);                        // left of the seam
        std::copy(s + cut + cellSize, s + inRow, d + cut); // right of the seam
    }
    return dst;
}
} // namespace

Image removeVerticalSeam(const Image& img, const Seam& seam)
{
    Image out;
    out.width = img.width - 1;
    out.height = img.height;
    out.px = removeSeamFromGrid(img.px, img.height, img.width, 4, seam);
    return out;
}

std::vector<std::uint8_t> seamPixels(const Image& img, const Seam& seam)
{
    std::vector<std::uint8_t> px(static_cast<size_t>(img.height) * 4);
    for (int i = 0; i < img.height; ++i)
        std::copy(img.at(i, seam[i]), img.at(i, seam[i]) + 4, &px[static_cast<size_t>(i) * 4]);
    return px;
}

Image insertVerticalSeam(const Image& img, const Seam& seam, const std::vector<std::uint8_t>& pixels)
{
    Image out(img.width + 1, img.height);
    for (int i = 0; i < img.height; ++i)
    {
        const std::uint8_t* s = img.row(i);
        std::uint8_t* d = out.row(i);
        const size_t cut = static_cast<size_t>(seam[i]) * 4;
        std::copy(s, s + cut, d);
        std::copy(&pixels[static_cast<size_t>(i) * 4], &pixels[static_cast<size_t>(i) * 4] + 4, d + cut);
        std::copy(s + cut, s + static_cast<size_t>(img.width) * 4, d + cut + 4);
    }
    return out;
}

Image carveVertical(const Image& img, int k, const SeamFinder& finder,
                    CarveStats& stats, Image* seamOverlay, bool incrementalEnergy)
{
    k = std::max(0, std::min(k, img.width - 1));

    Image cur = img;

    // origCol[i * cols + j] = column in the ORIGINAL image of the pixel now at (i, j).
    // Lets us draw every removed seam back onto the original for comparison.
    int cols = img.width;
    std::vector<int> origCol(static_cast<size_t>(img.height) * img.width);
    for (int i = 0; i < img.height; ++i)
        for (int j = 0; j < img.width; ++j)
            origCol[static_cast<size_t>(i) * img.width + j] = j;

    if (seamOverlay)
        *seamOverlay = img;

    EnergyMap e;
    if (incrementalEnergy && k > 0)
    {
        const auto t0 = Clock::now();
        e = computeEnergy(cur); // full map once, then only updated
        stats.energyMs += msSince(t0);
    }

    for (int s = 0; s < k; ++s)
    {
        auto t0 = Clock::now();
        if (!incrementalEnergy)
        {
            e = computeEnergy(cur);
            stats.energyMs += msSince(t0);
        }

        t0 = Clock::now();
        Seam seam = finder(e);
        stats.findMs += msSince(t0);

        stats.totalSeamCost += seamCost(e, seam);

        if (seamOverlay)
        {
            for (int i = 0; i < cur.height; ++i)
            {
                std::uint8_t* p = seamOverlay->at(i, origCol[static_cast<size_t>(i) * cols + seam[i]]);
                p[0] = 255; p[1] = 0; p[2] = 0; p[3] = 255; // red
            }
        }

        t0 = Clock::now();
        cur = removeVerticalSeam(cur, seam);
        origCol = removeSeamFromGrid(origCol, cur.height, cols, 1, seam);
        --cols;
        stats.removeMs += msSince(t0);

        if (incrementalEnergy)
        {
            t0 = Clock::now();
            updateEnergyAfterSeamRemoval(e, cur, seam);
            stats.energyMs += msSince(t0);
        }

        ++stats.seamsRemoved;
    }
    return cur;
}

Image carveHorizontal(const Image& img, int k, const SeamFinder& finder,
                      CarveStats& stats, Image* seamOverlay, bool incrementalEnergy)
{
    const Image t = transposeImage(img);
    Image overlayT;
    const Image resultT = carveVertical(t, k, finder, stats, seamOverlay ? &overlayT : nullptr, incrementalEnergy);
    if (seamOverlay)
        *seamOverlay = transposeImage(overlayT);
    return transposeImage(resultT);
}
