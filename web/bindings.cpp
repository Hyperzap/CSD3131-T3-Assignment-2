// JavaScript bindings for the seam carving core (Emscripten embind).
//
// Build:  emcmake cmake -B build-web && cmake --build build-web
// Output: web/dist/seam.js + web/dist/seam.wasm (ES module, factory createSeamModule)
//
// JS usage (see docs/API.md and web/seam.d.ts):
//   import createSeamModule from './dist/seam.js';
//   const M = await createSeamModule();
//   const c = new M.Carver(width, height, imageData.data);   // RGBA bytes
//   c.stepForward('dp', false);   // seam #1 found (pending) -> c.pending()
//   c.stepForward('dp', false);   // seam #1 removed         -> c.image()
//   c.delete();                   // embind objects are not garbage-collected
//
// Every array handed to JS is a COPY (safe to keep and to transfer between
// workers); nothing points into WASM memory.

#include "carver.h"

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <string>
#include <vector>

using emscripten::val;

namespace
{
const char* VERSION = "1.0.0";

// Copies a C++ buffer into a new JS typed array (e.g. "Uint8Array").
template <typename T>
val copyToJs(const char* arrayType, const T* data, size_t count)
{
    return val::global(arrayType).new_(emscripten::typed_memory_view(count, data));
}

val stepToJs(const CarveStep& s, int index)
{
    val o = val::object();
    o.set("index", index); // 0-based position in the removal order
    o.set("horizontal", s.horizontal);
    o.set("algo", std::string(algoId(s.algo)));
    o.set("cost", s.cost);
    o.set("findMs", s.findMs);
    o.set("path", copyToJs("Int32Array", s.seam.data(), s.seam.size()));
    return o;
}

Algo algoOrThrow(const std::string& name)
{
    Algo a;
    if (!parseAlgo(name, a))
        val::global("Error").new_(std::string("Unknown algorithm '") + name + "' (use dp, greedy or graphcut)").throw_();
    return a;
}
} // namespace

// Thin wrapper around Carver that speaks JS types.
class WebCarver
{
public:
    // rgba: Uint8Array or Uint8ClampedArray (e.g. ImageData.data), width*height*4 bytes.
    WebCarver(int width, int height, const val& rgba) : carver_(fromJs(width, height, rgba)) {}

    int width() const { return carver_.image().width; }
    int height() const { return carver_.image().height; }
    int originalWidth() const { return carver_.original().width; }
    int originalHeight() const { return carver_.original().height; }

    // Two-phase stepping (see carver.h). All return false when nothing happened;
    // lastError() says why.
    bool findSeam(const std::string& algo, bool horizontal) { return carver_.findSeam(algoOrThrow(algo), horizontal); }
    bool removeSeam() { return carver_.removeSeam(); }
    bool stepForward(const std::string& algo, bool horizontal) { return carver_.stepForward(algoOrThrow(algo), horizontal); }
    bool stepBack() { return carver_.stepBack(); }
    void discardFuture() { carver_.discardFuture(); }
    void reset() { carver_.reset(); }

    // Removes `cols` vertical then `rows` horizontal seams in one go.
    // Returns the number of seams actually removed.
    int carve(const std::string& algo, int cols, int rows)
    {
        const Algo a = algoOrThrow(algo);
        carver_.discardFuture();
        int done = 0;
        for (int k = 0; k < cols && carver_.findSeam(a, false) && carver_.removeSeam(); ++k) ++done;
        for (int k = 0; k < rows && carver_.findSeam(a, true) && carver_.removeSeam(); ++k) ++done;
        return done;
    }

    bool hasPending() const { return carver_.hasPending(); }
    // The highlighted (found, not yet removed) seam, or null.
    val pending() const
    {
        const CarveStep* p = carver_.pending();
        return p ? stepToJs(*p, carver_.stepsRemoved()) : val::null();
    }
    int stepsRemoved() const { return carver_.stepsRemoved(); }
    // Removed step k (0 = first removed), or null if out of range.
    val step(int k) const
    {
        return (k >= 0 && k < carver_.stepsRemoved()) ? stepToJs(carver_.step(k), k) : val::null();
    }
    // All removed steps, oldest first.
    val steps() const
    {
        val arr = val::array();
        for (int k = 0; k < carver_.stepsRemoved(); ++k)
            arr.call<void>("push", stepToJs(carver_.step(k), k));
        return arr;
    }

    // Current image as RGBA bytes (width() * height() * 4).
    val image() const
    {
        const Image& img = carver_.image();
        return copyToJs("Uint8ClampedArray", img.px.data(), img.px.size());
    }
    // Energy map of the current image as a grayscale RGBA image (for display).
    val energyImage() { return toRgba(energyToImage(carver_.energy())); }
    // Raw energy values (width() * height() doubles, row-major).
    val energy()
    {
        const EnergyMap& e = carver_.energy();
        return copyToJs("Float64Array", e.data.data(), e.data.size());
    }

    std::string lastError() const { return carver_.lastError(); }

private:
    static Image fromJs(int width, int height, const val& rgba)
    {
        if (width <= 0 || height <= 0)
            val::global("Error").new_(std::string("width and height must be positive")).throw_();
        const unsigned expected = static_cast<unsigned>(width) * static_cast<unsigned>(height) * 4u;
        if (rgba["length"].as<unsigned>() != expected)
            val::global("Error").new_(std::string("rgba must have width*height*4 = ") + std::to_string(expected) + " bytes").throw_();

        Image img(width, height);
        val view(emscripten::typed_memory_view(img.px.size(), img.px.data()));
        view.call<void>("set", rgba); // one bulk copy from JS into WASM memory
        return img;
    }

    static val toRgba(const Image& img) { return copyToJs("Uint8ClampedArray", img.px.data(), img.px.size()); }

    Carver carver_;
};

std::string version() { return VERSION; }

EMSCRIPTEN_BINDINGS(seamcarve)
{
    emscripten::function("version", &version);

    emscripten::class_<WebCarver>("Carver")
        .constructor<int, int, const val&>()
        .function("width", &WebCarver::width)
        .function("height", &WebCarver::height)
        .function("originalWidth", &WebCarver::originalWidth)
        .function("originalHeight", &WebCarver::originalHeight)
        .function("findSeam", &WebCarver::findSeam)
        .function("removeSeam", &WebCarver::removeSeam)
        .function("stepForward", &WebCarver::stepForward)
        .function("stepBack", &WebCarver::stepBack)
        .function("discardFuture", &WebCarver::discardFuture)
        .function("reset", &WebCarver::reset)
        .function("carve", &WebCarver::carve)
        .function("hasPending", &WebCarver::hasPending)
        .function("pending", &WebCarver::pending)
        .function("stepsRemoved", &WebCarver::stepsRemoved)
        .function("step", &WebCarver::step)
        .function("steps", &WebCarver::steps)
        .function("image", &WebCarver::image)
        .function("energyImage", &WebCarver::energyImage)
        .function("energy", &WebCarver::energy)
        .function("lastError", &WebCarver::lastError);
}
