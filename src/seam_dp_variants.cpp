#include "seam.h"

#include <cstdint>
#include <limits>

#ifdef _OPENMP
#include <omp.h>
#endif

// Part 5(d) improvements to the DP seam finder.
// Both variants make exactly the same choices (same tie-breaking) as
// findVerticalSeamDP, so they return identical seams; only speed/memory differ.

// ---------------------------------------------------------------------------
// 1) Compact DP: "do we need to maintain the entire DP table?"
//
// No. Row i of the cost table M only reads row i-1, so two rows are enough:
// O(n) doubles instead of O(mn).
// We still need to backtrack, so we keep one decision per cell, but only as
// an int8 offset {-1, 0, +1} instead of an int column (or a double cost).
//   full table:  m*n*(8 + 4) bytes   (double M + int parent)
//   compact:     2*n*8 + m*n*1 bytes  -> about 12x less memory
// (Going below O(mn) is possible, e.g. Hirschberg-style divide and conquer
//  recomputing rows, but that trades memory for extra time.)
// ---------------------------------------------------------------------------
Seam findVerticalSeamDPCompact(const EnergyMap& e)
{
    const int m = e.rows;
    const int n = e.cols;

    std::vector<double> prev(n), cur(n);
    std::vector<std::int8_t> offset(static_cast<size_t>(m) * n, 0);

    for (int j = 0; j < n; ++j)
        prev[j] = e(0, j);

    for (int i = 1; i < m; ++i)
    {
        std::int8_t* off = &offset[static_cast<size_t>(i) * n];
        for (int j = 0; j < n; ++j)
        {
            double best = prev[j];
            std::int8_t o = 0;
            if (j > 0 && prev[j - 1] < best) { best = prev[j - 1]; o = -1; }
            if (j + 1 < n && prev[j + 1] < best) { best = prev[j + 1]; o = +1; }
            cur[j] = e(i, j) + best;
            off[j] = o;
        }
        prev.swap(cur);
    }

    int j = 0;
    for (int c = 1; c < n; ++c)
        if (prev[c] < prev[j])
            j = c;

    Seam seam(m);
    for (int i = m - 1; i >= 0; --i)
    {
        seam[i] = j;
        j += offset[static_cast<size_t>(i) * n + j];
    }
    return seam;
}

// ---------------------------------------------------------------------------
// 2) Parallel DP: "does multi-threading help?"
//
// Rows must be processed in order (row i needs row i-1), but all n cells of a
// row are independent, so each row is split across threads with a barrier
// between rows. That's m barriers per seam and only O(n) work between them,
// so on narrow images the synchronisation can cost more than it saves.
// Measure it with seambench.
//
// Uses the compact memory layout; the two cost rows are picked by parity
// (row i writes buf[i % 2], reads buf[(i-1) % 2]) so no swap is needed
// between threads.
// ---------------------------------------------------------------------------
Seam findVerticalSeamDPParallel(const EnergyMap& e, int threads)
{
    const int m = e.rows;
    const int n = e.cols;

    std::vector<double> buf(2 * static_cast<size_t>(n));
    std::vector<std::int8_t> offset(static_cast<size_t>(m) * n, 0);

    for (int j = 0; j < n; ++j)
        buf[j] = e(0, j);

#ifdef _OPENMP
    if (threads <= 0)
        threads = omp_get_max_threads();
#pragma omp parallel num_threads(threads)
#else
    (void)threads;
#endif
    {
        for (int i = 1; i < m; ++i)
        {
            const double* prev = &buf[static_cast<size_t>((i - 1) & 1) * n];
            double* cur = &buf[static_cast<size_t>(i & 1) * n];
            std::int8_t* off = &offset[static_cast<size_t>(i) * n];

#ifdef _OPENMP
#pragma omp for schedule(static)
#endif
            for (int j = 0; j < n; ++j)
            {
                double best = prev[j];
                std::int8_t o = 0;
                if (j > 0 && prev[j - 1] < best) { best = prev[j - 1]; o = -1; }
                if (j + 1 < n && prev[j + 1] < best) { best = prev[j + 1]; o = +1; }
                cur[j] = e(i, j) + best;
                off[j] = o;
            }
            // implicit barrier at the end of "omp for": row i is complete
        }
    }

    const double* last = &buf[static_cast<size_t>((m - 1) & 1) * n];
    int j = 0;
    for (int c = 1; c < n; ++c)
        if (last[c] < last[j])
            j = c;

    Seam seam(m);
    for (int i = m - 1; i >= 0; --i)
    {
        seam[i] = j;
        j += offset[static_cast<size_t>(i) * n + j];
    }
    return seam;
}

int maxThreads()
{
#ifdef _OPENMP
    return omp_get_max_threads();
#else
    return 1;
#endif
}
