#include "seam.h"

// Greedy seam finder (Part 3).
// Purely local decisions: pick the cheapest pixel in the top row, then at each
// step move to whichever of the 3 pixels below is cheapest. It never looks
// ahead, so it can walk into a high-energy region it can't escape from.
// Ties prefer going straight down.
Seam findVerticalSeamGreedy(const EnergyMap& e)
{
    const int m = e.rows;
    const int n = e.cols;

    Seam seam(m);

    int j = 0;
    for (int c = 1; c < n; ++c)
        if (e(0, c) < e(0, j))
            j = c;
    seam[0] = j;

    for (int i = 1; i < m; ++i)
    {
        int bestJ = j;
        double best = e(i, j);
        if (j > 0 && e(i, j - 1) < best)
        {
            best = e(i, j - 1);
            bestJ = j - 1;
        }
        if (j + 1 < n && e(i, j + 1) < best)
        {
            best = e(i, j + 1);
            bestJ = j + 1;
        }
        j = bestJ;
        seam[i] = j;
    }
    return seam;
}
