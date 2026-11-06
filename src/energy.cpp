#include "seam.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace
{
// Energy of one pixel: 3x3 Sobel in x and y with replicated borders,
// |gx| + |gy| summed over R, G, B (alpha ignored).
//   gx kernel: [-1 0 1; -2 0 2; -1 0 1]     gy kernel: its transpose
// up / mid / dn = the rows above, at and below i (already clamped to the image),
// c0 / c2 = the columns left and right of j (clamped).
inline double pixelEnergyRows(const std::uint8_t* up, const std::uint8_t* mid, const std::uint8_t* dn,
                              int c0, int j, int c2)
{
    const int a = c0 * 4, b = j * 4, d = c2 * 4;
    int sum = 0;
    for (int c = 0; c < 3; ++c)
    {
        const int gx = (up[d + c] + 2 * mid[d + c] + dn[d + c]) - (up[a + c] + 2 * mid[a + c] + dn[a + c]);
        const int gy = (dn[a + c] + 2 * dn[b + c] + dn[d + c]) - (up[a + c] + 2 * up[b + c] + up[d + c]);
        sum += std::abs(gx) + std::abs(gy);
    }
    return static_cast<double>(sum);
}

inline double pixelEnergy(const Image& img, int i, int j)
{
    const int r0 = std::max(i - 1, 0), r2 = std::min(i + 1, img.height - 1);
    const int c0 = std::max(j - 1, 0), c2 = std::min(j + 1, img.width - 1);
    return pixelEnergyRows(img.row(r0), img.row(i), img.row(r2), c0, j, c2);
}
} // namespace

EnergyMap computeEnergy(const Image& img)
{
    EnergyMap e;
    e.rows = img.height;
    e.cols = img.width;
    e.data.resize(static_cast<size_t>(e.rows) * e.cols);

    const int last = img.width - 1;
    for (int i = 0; i < img.height; ++i)
    {
        const std::uint8_t* up = img.row(std::max(i - 1, 0));
        const std::uint8_t* mid = img.row(i);
        const std::uint8_t* dn = img.row(std::min(i + 1, img.height - 1));
        double* out = &e.data[static_cast<size_t>(i) * e.cols];
        for (int j = 0; j < img.width; ++j)
            out[j] = pixelEnergyRows(up, mid, dn, j > 0 ? j - 1 : 0, j, j < last ? j + 1 : last);
    }
    return e;
}

Image energyToImage(const EnergyMap& e)
{
    Image out(e.cols, e.rows);
    if (e.data.empty())
        return out;
    const auto mm = std::minmax_element(e.data.begin(), e.data.end());
    const double lo = *mm.first, range = *mm.second - *mm.first;
    for (size_t k = 0; k < e.data.size(); ++k)
    {
        const double v = range > 0 ? (e.data[k] - lo) * 255.0 / range : 0.0;
        const auto g = static_cast<std::uint8_t>(std::lround(v));
        std::uint8_t* p = &out.px[k * 4];
        p[0] = p[1] = p[2] = g;
        p[3] = 255;
    }
    return out;
}

EnergyMap transposeEnergy(const EnergyMap& e)
{
    EnergyMap t;
    t.rows = e.cols;
    t.cols = e.rows;
    t.data.resize(e.data.size());
    for (int i = 0; i < e.rows; ++i)
        for (int j = 0; j < e.cols; ++j)
            t(j, i) = e(i, j);
    return t;
}

Image transposeImage(const Image& img)
{
    Image t(img.height, img.width);
    for (int i = 0; i < img.height; ++i)
        for (int j = 0; j < img.width; ++j)
            std::copy(img.at(i, j), img.at(i, j) + 4, t.at(j, i));
    return t;
}

double seamCost(const EnergyMap& e, const Seam& seam)
{
    double total = 0.0;
    for (int i = 0; i < e.rows; ++i)
        total += e(i, seam[i]);
    return total;
}

// ---------------------------------------------------------------------------
// Incremental energy update (Part 5d).
//
// The energy of a pixel depends only on its 3x3 neighbourhood. After removing
// a seam, a pixel's energy can only change if its neighbourhood changed, i.e.
// some neighbour shifted differently from the pixel itself:
//   - pixel left of the seam in row i (not shifted): unchanged if its right
//     neighbours in rows i-1..i+1 are also left of the seam there:
//         j + 1 < s_r  for r = i-1, i, i+1   <=>  j < min(s) - 1
//   - pixel right of the seam (shifted left by 1): unchanged if
//         j - 1 >= s_r for r = i-1, i, i+1   <=>  j >= max(s) + 1
// So only columns [min(s) - 1, max(s)] of row i need recomputing, where s
// ranges over the seam columns of rows i-1, i, i+1. That's usually ~3 pixels
// per row: O(m) work instead of O(mn) per seam.
// (Borders are replicated, which keeps this argument valid at the edges.)
// ---------------------------------------------------------------------------
void updateEnergyAfterSeamRemoval(EnergyMap& e, const Image& newImg, const Seam& seam)
{
    const int m = e.rows;
    const int n = e.cols;
    const int nn = n - 1;

    // 1) Remove the seam from the energy map (same shift as the image).
    EnergyMap out;
    out.rows = m;
    out.cols = nn;
    out.data.resize(static_cast<size_t>(m) * nn);
    for (int i = 0; i < m; ++i)
    {
        const double* src = &e.data[static_cast<size_t>(i) * n];
        double* dst = &out.data[static_cast<size_t>(i) * nn];
        const int s = seam[i];
        std::copy(src, src + s, dst);
        std::copy(src + s + 1, src + n, dst + s);
    }

    // 2) Recompute only the band of pixels whose neighbourhood changed.
    for (int i = 0; i < m; ++i)
    {
        const int sa = seam[std::max(i - 1, 0)];
        const int sb = seam[i];
        const int sc = seam[std::min(i + 1, m - 1)];
        const int lo = std::max(std::min({sa, sb, sc}) - 1, 0);
        const int hi = std::min(std::max({sa, sb, sc}), nn - 1);
        for (int j = lo; j <= hi; ++j)
            out(i, j) = pixelEnergy(newImg, i, j);
    }

    e = std::move(out);
}
