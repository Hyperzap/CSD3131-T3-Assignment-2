#include "seam.h"

#include <algorithm>
#include <cmath>
#include <vector>

// Graph cut seam finder (Part 4): seam finding as a minimum s-t cut.
//
// Graph for a vertical seam on an m x n energy map:
//   nodes: one per pixel (i, j), plus one DUMMY column j = n per row, plus S and T
//   S -> (i, 0)            capacity INF
//   (i, n) -> T            capacity INF                     (dummy column -> sink)
//   (i, j) -> (i, j+1)     capacity e(i, j)                 cost of removing pixel (i, j)
//   (i, j+1) -> (i, j)     capacity INF                     backward edge
//   (i, j) -> (i-1, j-1)   capacity INF                     diagonal edges: force the
//   (i, j) -> (i+1, j-1)   capacity INF                     cut to be a connected seam
//
// Why it works: a finite cut can't cross any INF edge, so
//   * backward edges  -> the source side of each row is a prefix [0 .. c_i]
//   * S / T edges     -> 0 <= c_i <= n-1
//   * diagonal edges  -> |c_i - c_(i-1)| <= 1   (8-connected seam)
// and the cut capacity is exactly sum_i e(i, c_i) = the seam's cost. So the
// min cut IS the min-cost seam, and by max-flow/min-cut its capacity equals
// the max flow value (handy self-check: it must equal the DP seam cost).
//
// The dummy column: the spec connects the LAST pixel column to T. Then the
// last column could never be on the source side, so pixels in column n-1 could
// never be removed. Adding one dummy column (and connecting that to T instead)
// gives exactly the same set of seams as the DP, so the results are comparable.
//
// Max-flow: Dinic's algorithm (BFS level graph + blocking flow), with an
// iterative DFS so deep augmenting paths can't overflow the stack.
// O(V^2 E) worst case, far better in practice than Edmonds-Karp's O(V E^2).

namespace
{
using ll = long long;

// "Infinite" capacity: bigger than any possible finite cut, small enough
// that adding flows to it can never overflow a 64-bit integer.
constexpr ll INF = 1LL << 50;

// Capacities are integers so max-flow is exact (no floating-point epsilon
// problems). The Sobel energy of an 8-bit image is already an integer, so
// SCALE = 1 is exact. If you switch to a non-integer energy, raise SCALE.
constexpr double SCALE = 1.0;

struct Dinic
{
    std::vector<int> head, nxt, to, level, it;
    std::vector<ll> cap;

    Dinic(int nodes, size_t arcReserve) : head(nodes, -1), level(nodes), it(nodes)
    {
        nxt.reserve(arcReserve);
        to.reserve(arcReserve);
        cap.reserve(arcReserve);
    }

    // Adds u->v with capacity c and v->u with capacity rc.
    // The pair lives at indices (k, k^1), so the reverse arc of e is e ^ 1.
    void addEdge(int u, int v, ll c, ll rc = 0)
    {
        to.push_back(v); cap.push_back(c);  nxt.push_back(head[u]); head[u] = static_cast<int>(to.size()) - 1;
        to.push_back(u); cap.push_back(rc); nxt.push_back(head[v]); head[v] = static_cast<int>(to.size()) - 1;
    }

    // Builds the level graph. Returns whether T is still reachable.
    // When it returns false, level[v] >= 0 marks exactly the nodes reachable
    // from S in the residual graph = the source side of the min cut.
    bool bfs(int s, int t)
    {
        std::fill(level.begin(), level.end(), -1);
        std::vector<int> q;
        q.reserve(head.size());
        q.push_back(s);
        level[s] = 0;
        for (size_t qi = 0; qi < q.size(); ++qi)
        {
            const int u = q[qi];
            for (int e = head[u]; e != -1; e = nxt[e])
            {
                if (cap[e] > 0 && level[to[e]] < 0)
                {
                    level[to[e]] = level[u] + 1;
                    q.push_back(to[e]);
                }
            }
        }
        return level[t] >= 0;
    }

    // Blocking flow on the current level graph (iterative DFS).
    ll blockingFlow(int s, int t)
    {
        ll total = 0;
        std::vector<int> path; // arcs on the current S -> u path
        int u = s;
        while (true)
        {
            if (u == t)
            {
                ll f = INF;
                for (int e : path) f = std::min(f, cap[e]);
                size_t firstSaturated = path.size();
                for (size_t k = 0; k < path.size(); ++k)
                {
                    cap[path[k]] -= f;
                    cap[path[k] ^ 1] += f;
                    if (firstSaturated == path.size() && cap[path[k]] == 0)
                        firstSaturated = k;
                }
                total += f;
                // Retreat to the tail of the first saturated arc and continue from there.
                path.resize(firstSaturated);
                u = path.empty() ? s : to[path.back()];
                continue;
            }

            // Advance along the next admissible arc, if any.
            int& e = it[u];
            while (e != -1 && !(cap[e] > 0 && level[to[e]] == level[u] + 1))
                e = nxt[e];

            if (e != -1)
            {
                path.push_back(e);
                u = to[e];
            }
            else
            {
                // Dead end: prune u from the level graph and step back.
                level[u] = -1;
                if (path.empty())
                    break;
                const int back = path.back();
                path.pop_back();
                u = to[back ^ 1];
                it[u] = nxt[it[u]]; // skip the arc that led to the dead end
            }
        }
        return total;
    }

    ll maxFlow(int s, int t)
    {
        ll flow = 0;
        while (bfs(s, t))
        {
            it = head;
            flow += blockingFlow(s, t);
        }
        return flow; // the final failed bfs() left the residual-reachable set in level[]
    }
};
} // namespace

Seam findVerticalSeamGraphCutEx(const EnergyMap& e, double* maxFlowOut)
{
    const int m = e.rows;
    const int n = e.cols;
    const int W = n + 1; // pixel columns + 1 dummy column
    auto id = [W](int i, int j) { return i * W + j; };
    const int S = m * W;
    const int T = S + 1;

    Dinic g(m * W + 2, static_cast<size_t>(m) * W * 6 + 4 * m);

    for (int i = 0; i < m; ++i)
    {
        g.addEdge(S, id(i, 0), INF);
        g.addEdge(id(i, n), T, INF);

        for (int j = 0; j < n; ++j)
        {
            // Forward edge with cost e(i, j) and its INF backward edge,
            // stored as one arc pair: (i,j)->(i,j+1) cap e, (i,j+1)->(i,j) cap INF.
            const ll c = static_cast<ll>(std::llround(e(i, j) * SCALE));
            g.addEdge(id(i, j), id(i, j + 1), c, INF);

            if (j > 0)
            {
                if (i > 0)     g.addEdge(id(i, j), id(i - 1, j - 1), INF);
                if (i + 1 < m) g.addEdge(id(i, j), id(i + 1, j - 1), INF);
            }
        }
    }

    const ll flow = g.maxFlow(S, T);
    if (maxFlowOut)
        *maxFlowOut = static_cast<double>(flow) / SCALE;

    // Source side of the min cut = nodes reachable from S in the residual graph.
    // In each row the seam pixel is the rightmost pixel on the source side.
    Seam seam(m, 0);
    for (int i = 0; i < m; ++i)
    {
        for (int j = n - 1; j >= 0; --j)
        {
            if (g.level[id(i, j)] >= 0)
            {
                seam[i] = j;
                break;
            }
        }
    }
    return seam;
}

Seam findVerticalSeamGraphCut(const EnergyMap& e)
{
    return findVerticalSeamGraphCutEx(e, nullptr);
}
