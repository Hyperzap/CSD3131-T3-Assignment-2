// Seam carving benchmark suite (native C++, no dependencies beyond the engine in src/).
//
// Build and run (from the repo root):
//   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
//   cmake --build build --config Release
//   build/seambench                     (Windows: build\Release\seambench.exe)
//
// Options:
//   --out DIR          where to write the CSV files and summary.md   (default: bench-results)
//   --sizes A,B,...    longest image side for each test size          (default: 256,512,1024,2048)
//   --seams N          seams removed in the carving test              (default: 50)
//   --graphcut-max S   largest size Graph Cut is timed at (it is slow) (default: 512)
//   --image FILE.ppm   use your own photo (binary PPM, "P6") instead of the generated scene
//   --quick            fewer repetitions and sizes, for a fast check
//
// What it measures:
//   1. DP seam finder: full table vs compact (2 rows + int8) vs OpenMP threads, plus table memory
//   2. DP vs Greedy vs Graph Cut: time per seam and seam cost
//   3. Carving N seams: full energy recompute vs incremental update
// Every timed variant is first checked against the baseline (same seam / same image / same
// cost), and a few extra correctness checks run at the end. Any failure makes the program
// exit with code 2, so CI notices.

#include "carve.h"
#include "carver.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace
{
// ---------------------------------------------------------------------------
// Test images
// ---------------------------------------------------------------------------

// Deterministic pseudo-random numbers (same scene on every machine).
struct Lcg
{
    std::uint32_t s;
    explicit Lcg(std::uint32_t seed) : s(seed) {}
    int next(int range) { s = s * 1664525u + 1013904223u; return static_cast<int>((s >> 8) % static_cast<std::uint32_t>(range)); }
};

void setPx(Image& img, int i, int j, int r, int g, int b)
{
    std::uint8_t* p = img.at(i, j);
    p[0] = static_cast<std::uint8_t>(std::clamp(r, 0, 255));
    p[1] = static_cast<std::uint8_t>(std::clamp(g, 0, 255));
    p[2] = static_cast<std::uint8_t>(std::clamp(b, 0, 255));
    p[3] = 255;
}

// A landscape like the website's sample: smooth sky and sea (cheap to carve), a sun, two
// ridges and a striped tower (expensive), plus faint noise so real photos' texture is
// imitated and DP does not just meet long runs of exact ties.
Image makeScene(int w, int h)
{
    Image img(w, h);
    Lcg rng(12345u);
    const double horizon = h * 0.68;
    const double sunX = w * 0.27, sunY = horizon * 0.72, sunR = h * 0.075;
    const int towerL = static_cast<int>(w * 0.70), towerR = static_cast<int>(w * 0.76);
    const int towerTop = static_cast<int>(h * 0.28);

    for (int i = 0; i < h; ++i)
    {
        for (int j = 0; j < w; ++j)
        {
            const int noise = rng.next(7) - 3;
            double r, g, b;
            if (i < horizon)
            {
                const double t = i / horizon; // sky: blue at the top, warm near the horizon
                r = 39 + t * (243 - 39); g = 68 + t * (192 - 68); b = 107 + t * (124 - 107);
                const double ridge1 = horizon - h * 0.17 * (0.55 + 0.45 * std::sin(j * 0.011 * 800.0 / w));
                const double ridge2 = horizon - h * 0.10 * (0.55 + 0.45 * std::sin(j * 0.023 * 800.0 / w + 1.3));
                if (i > ridge1) { r = 87; g = 80; b = 111; }
                if (i > ridge2) { r = 52; g = 58; b = 86; }
                const double dx = j - sunX, dy = i - sunY;
                if (dx * dx + dy * dy < sunR * sunR && i < ridge1) { r = 255; g = 230; b = 173; }
            }
            else
            {
                const double t = (i - horizon) / (h - horizon); // sea, with a few shimmer lines
                r = 40 - t * 20; g = 70 - t * 30; b = 100 - t * 40;
                if (std::abs(j - sunX) < w * 0.06 * (1.0 - t) && (i / 3) % 4 == 0) { r = 230; g = 200; b = 150; }
            }
            if (j >= towerL && j < towerR && i >= towerTop && i < horizon)
            {
                const bool red = ((i - towerTop) / std::max(1, h / 18)) % 2 == 0;
                r = red ? 180 : 240; g = red ? 50 : 235; b = red ? 50 : 225;
            }
            setPx(img, i, j, static_cast<int>(r) + noise, static_cast<int>(g) + noise, static_cast<int>(b) + noise);
        }
    }
    return img;
}

// Binary PPM ("P6", 8-bit) reader. Most image tools can save PPM (e.g. GIMP, IrfanView,
// ImageMagick: magick photo.jpg photo.ppm).
bool loadPPM(const std::string& path, Image& out, std::string& error)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) { error = "cannot open " + path; return false; }
    auto token = [&f]() {
        std::string t;
        char c;
        while (f.get(c))
        {
            if (c == '#') { std::string skip; std::getline(f, skip); continue; }
            if (std::isspace(static_cast<unsigned char>(c))) { if (!t.empty()) break; continue; }
            t += c;
        }
        return t;
    };
    if (token() != "P6") { error = path + " is not a binary PPM (P6) file"; return false; }
    const int w = std::stoi(token()), h = std::stoi(token()), maxval = std::stoi(token());
    if (w < 3 || h < 3 || maxval != 255) { error = path + ": need an 8-bit PPM of at least 3x3"; return false; }
    std::vector<unsigned char> rgb(static_cast<size_t>(w) * h * 3);
    f.read(reinterpret_cast<char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    if (!f) { error = path + ": file is truncated"; return false; }
    out = Image(w, h);
    for (size_t k = 0; k < static_cast<size_t>(w) * h; ++k)
    {
        out.px[k * 4 + 0] = rgb[k * 3 + 0];
        out.px[k * 4 + 1] = rgb[k * 3 + 1];
        out.px[k * 4 + 2] = rgb[k * 3 + 2];
        out.px[k * 4 + 3] = 255;
    }
    return true;
}

// Bilinear resize (only used to bring a --image photo to each test size).
Image resize(const Image& src, int w, int h)
{
    Image dst(w, h);
    for (int i = 0; i < h; ++i)
    {
        const double y = std::clamp((i + 0.5) * src.height / h - 0.5, 0.0, src.height - 1.0);
        const int y0 = static_cast<int>(y), y1 = std::min(y0 + 1, src.height - 1);
        const double fy = y - y0;
        for (int j = 0; j < w; ++j)
        {
            const double x = std::clamp((j + 0.5) * src.width / w - 0.5, 0.0, src.width - 1.0);
            const int x0 = static_cast<int>(x), x1 = std::min(x0 + 1, src.width - 1);
            const double fx = x - x0;
            for (int c = 0; c < 4; ++c)
            {
                const double v = (src.at(y0, x0)[c] * (1 - fx) + src.at(y0, x1)[c] * fx) * (1 - fy) +
                                 (src.at(y1, x0)[c] * (1 - fx) + src.at(y1, x1)[c] * fx) * fy;
                dst.at(i, j)[c] = static_cast<std::uint8_t>(std::lround(v));
            }
        }
    }
    return dst;
}

// ---------------------------------------------------------------------------
// Timing, formatting, results
// ---------------------------------------------------------------------------

double msSince(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// Average ms per call: one warm-up, then repeat until at least minMs (and 3 calls) have run.
double timeIt(const std::function<void()>& fn, double minMs)
{
    fn();
    int reps = 0;
    const auto t0 = Clock::now();
    do { fn(); ++reps; } while (reps < 3 || msSince(t0) < minMs);
    return msSince(t0) / reps;
}

std::string fmt(const char* f, double v)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), f, v);
    return buf;
}

std::string mb(double bytes) { return fmt("%.2f MB", bytes / (1024.0 * 1024.0)); }

// A table that prints to the console and to the Markdown summary.
struct Table
{
    std::vector<std::string> header;
    std::vector<std::vector<std::string>> rows;

    void print(std::ostream& os) const
    {
        std::vector<size_t> w(header.size());
        for (size_t c = 0; c < header.size(); ++c) w[c] = header[c].size();
        for (const auto& r : rows) for (size_t c = 0; c < r.size(); ++c) w[c] = std::max(w[c], r[c].size());
        auto line = [&](const std::vector<std::string>& r) {
            for (size_t c = 0; c < r.size(); ++c) os << (c ? "  " : "") << r[c] << std::string(w[c] - r[c].size(), ' ');
            os << "\n";
        };
        line(header);
        for (const auto& r : rows) line(r);
    }
    void markdown(std::ostream& os) const
    {
        auto line = [&](const std::vector<std::string>& r) {
            os << "|";
            for (const auto& cell : r) os << " " << cell << " |";
            os << "\n";
        };
        line(header);
        os << "|";
        for (size_t c = 0; c < header.size(); ++c) os << " --- |";
        os << "\n";
        for (const auto& r : rows) line(r);
    }
};

std::string compilerName()
{
#if defined(_MSC_VER)
    return "MSVC " + std::to_string(_MSC_VER);
#elif defined(__clang__)
    return std::string("Clang ") + __clang_version__;
#elif defined(__GNUC__)
    return std::string("GCC ") + __VERSION__;
#else
    return "unknown compiler";
#endif
}

struct Options
{
    fs::path out = "bench-results";
    std::vector<int> sizes = {256, 512, 1024, 2048};
    int seams = 50;
    int graphcutMax = 512;
    std::string image;
    bool quick = false;
};

bool parseArgs(int argc, char** argv, Options& o)
{
    bool sizesGiven = false;
    for (int k = 1; k < argc; ++k)
    {
        const std::string a = argv[k];
        auto value = [&]() -> std::string {
            if (k + 1 >= argc) throw std::runtime_error("missing value after " + a);
            return argv[++k];
        };
        if (a == "--out") o.out = value();
        else if (a == "--seams") o.seams = std::stoi(value());
        else if (a == "--graphcut-max") o.graphcutMax = std::stoi(value());
        else if (a == "--image") o.image = value();
        else if (a == "--quick") o.quick = true;
        else if (a == "--sizes")
        {
            o.sizes.clear();
            std::stringstream ss(value());
            for (std::string s; std::getline(ss, s, ',');) if (!s.empty()) o.sizes.push_back(std::stoi(s));
            sizesGiven = true;
        }
        else if (a == "--help" || a == "-h") return false;
        else throw std::runtime_error("unknown option " + a);
    }
    if (o.quick && !sizesGiven) o.sizes = {256, 512};
    if (o.quick) { o.seams = std::min(o.seams, 20); o.graphcutMax = std::min(o.graphcutMax, 256); }
    for (int s : o.sizes) if (s < 16) throw std::runtime_error("sizes must be at least 16");
    return true;
}
} // namespace

int main(int argc, char** argv)
{
    Options opt;
    try
    {
        if (!parseArgs(argc, argv, opt))
        {
            std::cout << "Usage: seambench [--out DIR] [--sizes 256,512,1024,2048] [--seams N]\n"
                         "                 [--graphcut-max S] [--image photo.ppm] [--quick]\n";
            return 0;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "seambench: " << e.what() << " (see --help)\n";
        return 1;
    }

    Image photo;
    if (!opt.image.empty())
    {
        std::string err;
        if (!loadPPM(opt.image, photo, err)) { std::cerr << "seambench: " << err << "\n"; return 1; }
    }
    // Test image at a given longest side, keeping the photo's aspect ratio (scene: 3:2).
    auto testImage = [&](int longest) {
        const double aspect = photo.empty() ? 2.0 / 3.0 : static_cast<double>(photo.height) / photo.width;
        int w = longest, h = std::max(3, static_cast<int>(std::lround(longest * aspect)));
        if (h > longest) { h = longest; w = std::max(3, static_cast<int>(std::lround(longest / aspect))); }
        return photo.empty() ? makeScene(w, h) : resize(photo, w, h);
    };

    const double minMs = opt.quick ? 100.0 : 400.0;
    const int maxT = maxThreads();
    std::vector<int> threads;
    for (int t = 1; t < maxT; t *= 2) threads.push_back(t);
    threads.push_back(maxT);

    fs::create_directories(opt.out);
    std::ostringstream md;
    bool allOk = true;
    std::vector<std::string> failures;
    auto check = [&](bool ok, const std::string& what) {
        if (!ok) { allOk = false; failures.push_back(what); }
        return ok ? "yes" : "NO";
    };

#ifdef NDEBUG
    const std::string buildType = "Release";
#else
    const std::string buildType = "Debug (timings not meaningful)";
#endif
    const std::string input = photo.empty() ? "generated landscape scene" : opt.image;

    std::cout << "Seam carving benchmark\n"
              << "  input:    " << input << "\n"
              << "  threads:  " << maxT << (maxT == 1 ? " (built without OpenMP)" : "") << "\n"
              << "  compiler: " << compilerName() << ", " << buildType << "\n\n";
    md << "## Seam carving benchmark\n\n"
       << "Input: " << input << " · OpenMP threads: " << maxT << " · " << compilerName() << ", " << buildType << "\n\n";

    // ------------------------------------------------------------------ 1. DP variants
    {
        Table t;
        t.header = {"size", "variant", "ms/seam", "speedup", "DP memory", "same seam"};
        std::ofstream csv(opt.out / "dp.csv");
        csv << "width,height,variant,threads,ms_per_seam,speedup_vs_full,dp_memory_bytes,same_seam\n";

        for (int longest : opt.sizes)
        {
            const Image img = testImage(longest);
            const EnergyMap e = computeEnergy(img);
            const double m = e.rows, n = e.cols;
            const std::string size = std::to_string(e.cols) + "x" + std::to_string(e.rows);
            const Seam ref = findVerticalSeamDP(e);

            struct V { std::string name; int threads; double mem; std::function<Seam()> run; };
            std::vector<V> vs = {
                {"full table", 1, m * n * (8 + 4), [&] { return findVerticalSeamDP(e); }},
                {"compact (2 rows + int8)", 1, 2 * n * 8 + m * n, [&] { return findVerticalSeamDPCompact(e); }},
            };
            for (int th : threads)
                vs.push_back({"parallel " + std::to_string(th) + (th == 1 ? " thread" : " threads"), th,
                              2 * n * 8 + m * n, [&e, th] { return findVerticalSeamDPParallel(e, th); }});

            double base = 0.0;
            for (const V& v : vs)
            {
                const char* same = check(v.run() == ref, "DP " + v.name + " at " + size + " returned a different seam");
                const double ms = timeIt([&] { volatile size_t s = v.run().size(); (void)s; }, minMs);
                if (base == 0.0) base = ms;
                t.rows.push_back({size, v.name, fmt("%.3f", ms), fmt("%.2fx", base / ms), mb(v.mem), same});
                csv << e.cols << ',' << e.rows << ',' << v.name << ',' << v.threads << ',' << ms << ','
                    << base / ms << ',' << static_cast<long long>(v.mem) << ',' << (same[0] == 'y') << "\n";
            }
        }
        std::cout << "=== 1. DP seam finder: one seam ===\n";
        t.print(std::cout);
        std::cout << "\n";
        md << "### 1. DP seam finder: one seam\n\n";
        t.markdown(md);
        md << "\n";
    }

    // ------------------------------------------------------------------ 2. DP vs Greedy vs Graph Cut
    {
        Table t;
        t.header = {"size", "algorithm", "ms/seam", "seam cost", "vs DP cost", "check"};
        std::ofstream csv(opt.out / "algorithms.csv");
        csv << "width,height,algorithm,ms_per_seam,seam_cost,cost_vs_dp,check_passed\n";

        for (int longest : opt.sizes)
        {
            const Image img = testImage(longest);
            const EnergyMap e = computeEnergy(img);
            const std::string size = std::to_string(e.cols) + "x" + std::to_string(e.rows);

            const Seam dp = findVerticalSeamDPCompact(e);
            const double dpCost = seamCost(e, dp);
            const double dpMs = timeIt([&] { volatile size_t s = findVerticalSeamDPCompact(e).size(); (void)s; }, minMs);

            const Seam gr = findVerticalSeamGreedy(e);
            const double grCost = seamCost(e, gr);
            const double grMs = timeIt([&] { volatile size_t s = findVerticalSeamGreedy(e).size(); (void)s; }, minMs);

            auto row = [&](const std::string& name, double ms, double cost, const char* ok) {
                t.rows.push_back({size, name, fmt("%.3f", ms), fmt("%.0f", cost), fmt("%.2fx", cost / dpCost), ok});
                csv << e.cols << ',' << e.rows << ',' << name << ',' << ms << ',' << cost << ',' << cost / dpCost
                    << ',' << (ok[0] == 'y' || ok[0] == '-') << "\n";
            };
            row("DP", dpMs, dpCost, "-");
            row("Greedy", grMs, grCost, check(grCost >= dpCost, "Greedy beat DP at " + size + " (impossible)"));

            if (longest <= opt.graphcutMax)
            {
                // Graph Cut is slow: timed once, no repetitions.
                double flow = 0.0;
                const auto t0 = Clock::now();
                const Seam gc = findVerticalSeamGraphCutEx(e, &flow);
                const double gcMs = msSince(t0);
                const double gcCost = seamCost(e, gc);
                const bool ok = gcCost == dpCost && flow == dpCost;
                row("Graph Cut", gcMs, gcCost, check(ok, "Graph Cut cost/max-flow differs from DP at " + size));
            }
            else
            {
                t.rows.push_back({size, "Graph Cut", "skipped", "", "", "(above --graphcut-max)"});
            }
        }
        std::cout << "=== 2. DP vs Greedy vs Graph Cut: one seam ===\n";
        t.print(std::cout);
        std::cout << "\n";
        md << "### 2. DP vs Greedy vs Graph Cut: one seam\n\n";
        t.markdown(md);
        md << "\nGraph Cut must match DP's cost exactly (both are optimal; its max-flow equals the cut). "
              "Greedy can never be cheaper than DP.\n\n";
    }

    // ------------------------------------------------------------------ 3. carving N seams
    {
        Table t;
        t.header = {"size", "energy", "energy ms", "find ms", "remove ms", "total ms", "same result"};
        std::ofstream csv(opt.out / "energy.csv");
        csv << "width,height,seams,energy_mode,energy_ms,find_ms,remove_ms,total_ms,identical\n";

        for (int longest : opt.sizes)
        {
            const Image img = testImage(longest);
            const int k = std::min(opt.seams, img.width - 2);
            const std::string size = std::to_string(img.width) + "x" + std::to_string(img.height);

            Image results[2];
            for (int mode = 0; mode < 2; ++mode)
            {
                CarveStats st;
                const auto t0 = Clock::now();
                results[mode] = carveVertical(img, k, findVerticalSeamDPCompact, st, nullptr, mode == 1);
                const double total = msSince(t0);
                const char* same = mode == 0 ? "(baseline)"
                                             : check(results[0].px == results[1].px,
                                                     "incremental energy changed the carved image at " + size);
                const char* name = mode == 0 ? "full recompute" : "incremental";
                t.rows.push_back({size, name, fmt("%.1f", st.energyMs), fmt("%.1f", st.findMs),
                                  fmt("%.1f", st.removeMs), fmt("%.1f", total), same});
                csv << img.width << ',' << img.height << ',' << k << ',' << name << ',' << st.energyMs << ','
                    << st.findMs << ',' << st.removeMs << ',' << total << ',' << (mode == 0 || same[0] == 'y') << "\n";
            }
        }
        std::cout << "=== 3. Carving " << opt.seams << " vertical seams (compact DP) ===\n";
        t.print(std::cout);
        std::cout << "\n";
        md << "### 3. Carving " << opt.seams << " vertical seams (compact DP): full vs incremental energy\n\n";
        t.markdown(md);
        md << "\n";
    }

    // ------------------------------------------------------------------ 4. correctness checks
    {
        Table t;
        t.header = {"check", "passed"};

        // Incremental energy equals a full recompute after every single seam.
        {
            Image img = testImage(400);
            EnergyMap inc = computeEnergy(img);
            bool same = true;
            for (int s = 0; s < 60 && img.width > 3; ++s)
            {
                const Seam seam = findVerticalSeamDPCompact(inc);
                img = removeVerticalSeam(img, seam);
                updateEnergyAfterSeamRemoval(inc, img, seam);
                same = same && inc.data == computeEnergy(img).data;
            }
            t.rows.push_back({"incremental energy == full recompute after each of 60 seams",
                              check(same, "incremental energy drifted from a full recompute")});
        }
        // Undo restores the original exactly after mixed algorithms and directions.
        {
            const Image img = testImage(200);
            Carver c(img);
            const Algo algos[] = {Algo::DP, Algo::Greedy, Algo::DP, Algo::GraphCut};
            int stepsTaken = 0;
            for (int k = 0; k < 24; ++k)
            {
                const Algo a = algos[k % 4];
                const bool horizontal = (k / 3) % 2 == 1;
                if (c.findSeam(a, horizontal) && c.removeSeam()) ++stepsTaken;
            }
            while (c.stepBack()) {}
            t.rows.push_back({"undo restores the original after " + std::to_string(stepsTaken) + " mixed steps",
                              check(c.image().px == img.px && stepsTaken == 24, "undo did not restore the image")});
        }
        std::cout << "=== 4. Correctness checks ===\n";
        t.print(std::cout);
        std::cout << "\n";
        md << "### 4. Correctness checks\n\n";
        t.markdown(md);
        md << "\n";
    }

    md << (allOk ? "**All checks passed.**\n" : "**SOME CHECKS FAILED.**\n");
    for (const auto& f : failures) md << "- " << f << "\n";
    std::ofstream(opt.out / "summary.md") << md.str();

    std::cout << "Results written to " << fs::absolute(opt.out).string() << " (dp.csv, algorithms.csv, energy.csv, summary.md)\n";
    if (allOk) std::cout << "All checks passed.\n";
    else
    {
        std::cout << "SOME CHECKS FAILED:\n";
        for (const auto& f : failures) std::cout << "  - " << f << "\n";
    }
    return allOk ? 0 : 2;
}
