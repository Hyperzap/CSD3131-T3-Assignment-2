#include "seam.h"

#include <limits>

// Dynamic programming seam finder (Part 1b).
//
// Optimal substructure: the cheapest seam ending at (i, j) must consist of
// pixel (i, j) plus the cheapest seam ending at one of (i-1, j-1), (i-1, j),
// (i-1, j+1). Anything else could be swapped for that cheaper prefix.
//
// Full-table version: M and the parent table are both m x n.
// (Part 5d idea: M only ever needs the previous row, so it can be cut to
//  O(n) with two rolling rows. The parent table is still needed to backtrack,
//  but it can be stored as int8 offsets {-1, 0, +1} instead of ints.)
Seam findVerticalSeamDP(const EnergyMap& e)
{
    const int m = e.rows;
    const int n = e.cols;

    std::vector<double> M(static_cast<size_t>(m) * n);
    std::vector<int> parent(static_cast<size_t>(m) * n, -1);
    auto idx = [n](int i, int j) { return static_cast<size_t>(i) * n + j; };

    // Base case: first row.
    for (int j = 0; j < n; ++j)
        M[idx(0, j)] = e(0, j);

    // Fill row by row. Every cell in a row depends only on the previous row,
    // so this inner loop is safe to parallelise (Part 5d).
    for (int i = 1; i < m; ++i)
    {
        for (int j = 0; j < n; ++j)
        {
            int bestJ = j;
            double best = M[idx(i - 1, j)];
            if (j > 0 && M[idx(i - 1, j - 1)] < best)
            {
                best = M[idx(i - 1, j - 1)];
                bestJ = j - 1;
            }
            if (j + 1 < n && M[idx(i - 1, j + 1)] < best)
            {
                best = M[idx(i - 1, j + 1)];
                bestJ = j + 1;
            }
            M[idx(i, j)] = e(i, j) + best;
            parent[idx(i, j)] = bestJ;
        }
    }

    // The seam ends at the minimum of the last row.
    int j = 0;
    double best = std::numeric_limits<double>::infinity();
    for (int c = 0; c < n; ++c)
    {
        if (M[idx(m - 1, c)] < best)
        {
            best = M[idx(m - 1, c)];
            j = c;
        }
    }

    // Backtrack bottom-up.
    Seam seam(m);
    for (int i = m - 1; i >= 0; --i)
    {
        seam[i] = j;
        j = parent[idx(i, j)];
    }
    return seam;
}
