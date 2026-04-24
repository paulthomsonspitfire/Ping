# Polygon Room Geometry — Implementation Plan
**P!NG v2.8.0 (target)**  
**Status:** Planning  
**Prepared for:** Cursor / Claude 4.7 high-thinking implementation

---

## 1. The Problem

All non-rectangular room shapes in the current IR Synth engine (`Fan / Shoebox`, `Cylindrical`, `Cathedral`, `Octagonal`) are computed as perfect rectangular boxes. The `p.shape` string is only consumed by `shapeDen()`, which returns a simple reflection amplitude scalar. There is no geometric difference between a Cathedral and a Rectangle — the image-source algorithm runs the same triple-nested Cartesian loop for both.

The `FloorPlanComponent` already has correct polygon definitions for all shapes (used for the floor-plan visualiser and puck drag-clamping), but these polygons are never shared with the synthesis engine.

This plan corrects that by replacing the fake scalar approach with proper 2D polygon image-source method (ISM) for the four non-rectangular shapes, while leaving the `Rectangular` path completely unchanged.

---

## 2. Scope — What Changes, What Doesn't

### Changes
| Area | What changes |
|------|-------------|
| `IRSynthEngine.h` | New `IRSynthParams` fields for shape proportions; new `Wall2D` struct; new function declarations |
| `IRSynthEngine.cpp` | New `calcRefsPolygon()` replacing the nx/ny nested loop for non-rectangular shapes; updated `calcRT60()` and `renderFDNTail()` to use polygon area/perimeter; `applyModalBank()` disabled for non-rectangular |
| `IRSynthComponent.cpp/h` | New proportion sliders per shape (Nave %, Transept %, Taper, Corner Cut); shape rename "Cylindrical" → "Circular Hall"; removal of "L-shaped" |
| `FloorPlanComponent.cpp/h` | `roomPoly()` updated to be parametric; Circular Hall drawn as 16-gon explicitly; L-shaped case removed |
| `PluginProcessor.cpp` | Sidecar persistence for new params; backward compat migration for old shape strings |
| `Tests/` | New test file `Tests/PingPolygonTests.cpp`; new IR_27–IR_32, DSP_22 |
| `Tools/generate_factory_irs.cpp` | Regenerate any factory IRs that use non-rectangular shapes |
| `CLAUDE.md` | New section documenting polygon ISM architecture |

### Does NOT change
- `calcRefs()` — the existing rectangular ISM function. **Untouched.** `Rectangular` shape continues to use it exclusively.
- All existing golden values: IR_11, IR_14 are rectangular and will not change.
- All existing tests IR_01–IR_26, DSP_01–DSP_21 must continue to pass without modification.
- Signal flow, FDN structure, ER/tail split, 4-channel layout, +15 dB trim, silence trim, convolver loading — all unchanged.
- The `synthDirectPath`, `synthExtraPath` helpers route through the same shape dispatch as `synthMainPath`.

---

## 3. Shape Inventory

### New shape list (replaces the old one in `IRSynthEngine.h` comment)

| Shape string | Surfaces | Algorithm | New params | Notes |
|---|---|---|---|---|
| `"Rectangular"` | 6 (box) | `calcRefs` (unchanged) | — | No change whatsoever |
| `"Fan / Shoebox"` | 6 (4 angled walls + floor/ceil) | `calcRefsPolygon` | `shapeTaper` | Highest polygon order; 4 horizontal walls |
| `"Octagonal"` | 10 (8 walls + floor/ceil) | `calcRefsPolygon` | `shapeCornerCut` | 8 horizontal walls |
| `"Circular Hall"` | 18 (16 walls + floor/ceil) | `calcRefsPolygon` | `shapeCornerCut` | Renamed from "Cylindrical"; 16 horizontal walls |
| `"Cathedral"` | 14 (12 walls + floor/ceil) | `calcRefsPolygon` | `shapeNavePct`, `shapeTrptPct` | Cruciform; 12 horizontal walls |

### Removed shape
- `"L-shaped"` — removed entirely. Migration: old sidecars/presets with `shape = "L-shaped"` → load as `"Rectangular"`.

### Migration table for backward compat
| Old string | New string |
|---|---|
| `"L-shaped"` | `"Rectangular"` |
| `"Cylindrical"` | `"Circular Hall"` |
| All others | Unchanged |

Apply this migration in `PluginProcessor.cpp` immediately after reading the `shape` attribute from the sidecar XML, before passing to `IRSynthComponent::setParams()`.

---

## 4. New `IRSynthParams` Fields

Add these four fields to `IRSynthParams` in `IRSynthEngine.h`:

```cpp
// Shape proportion parameters (only used when shape != "Rectangular")
double shapeNavePct   = 0.30;   // Cathedral: nave half-width / total width [0.15–0.50]
double shapeTrptPct   = 0.35;   // Cathedral: transept arm half-depth / total depth [0.20–0.45]
double shapeTaper     = 0.30;   // Fan/Shoebox: stage-end narrowing factor [0.0–0.70]
double shapeCornerCut = 0.414;  // Octagonal/Circular Hall: chamfer depth [0.0–1.0]
                                 // 0.414 = regular octagon when W=D
```

**Ranges chosen to prevent degenerate geometry:**
- `shapeNavePct`: below 0.15 the nave becomes a very thin slot; above 0.50 the transepts vanish
- `shapeTrptPct`: below 0.20 the transepts are vestigial; above 0.45 they would overlap/extend outside bounding box
- `shapeTaper`: above 0.70 the stage-end front wall becomes very short (< 30% of back wall width)
- `shapeCornerCut`: 0 = rectangle, 1 = maximum chamfer; clamp to [0, 1]

**Sidecar persistence** (in `PluginProcessor.cpp`, `irSynthParamsToXml` / `irSynthParamsFromXml`):
```xml
<synthParams ... shapeNavePct="0.30" shapeTrptPct="0.35" 
                 shapeTaper="0.30" shapeCornerCut="0.414" .../>
```

Read with fallback to the defaults above for older sidecars that lack these attributes.

**These parameters do NOT auto-trigger IR recalculation.** They are `IRSynthParams` fields like `width` / `depth` / `height` — they mark the synth dirty, user must hit Calculate IR. Same as all other geometry parameters.

---

## 5. Architecture of the New Algorithm

### Key insight: 2D polygon ISM + existing vertical (nz) loop

All polygon shapes have **vertical walls** and **flat floor/ceiling**. This means:

- **Horizontal reflections** (x,y plane): handled by the new 2D polygon ISM
- **Vertical reflections** (z axis, floor/ceiling): handled by the existing `nz` loop, exactly as in `calcRefs`

These two dimensions are independent. The replacement `calcRefsPolygon` keeps the `nz` loop intact and replaces only the inner `nx/ny` Cartesian enumeration with the Borish 2D polygon image-source tree.

### Why this is correct

For a room with vertical walls, any image source can be described as:
- A 2D reflected position `(ix, iy)` from the polygon ISM (horizontal reflections)
- A 1D mirrored height `iz` from the nz formula (vertical reflections)
- Total 3D image source position: `(ix, iy, iz)`
- 3D distance: `sqrt((ix-rx)² + (iy-ry)² + (iz-rz)²)`

The floor and ceiling ARE rectangular (they ARE the polygon shape in plan, but they're flat surfaces), so the nz absorption formula `pow(rF, ceil(|nz|/2)) × pow(rC, floor(|nz|/2))` remains correct.

---

## 6. `Wall2D` Struct

Add this struct to `IRSynthEngine.h` (inside `#ifdef PING_TESTING_BUILD` guard):

```cpp
struct Wall2D {
    double x1, y1;          // wall start point (actual metres)
    double x2, y2;          // wall end point (actual metres)
    double nx, ny;           // inward unit normal (pointing INTO the room)
    double rAbs[N_BANDS];   // reflection coefficient per band (= 1 - absorption)
                             // Initially set to rW[b] for all polygon walls
};
```

**Convention:** Walls listed counter-clockwise when viewed from above (standard polygon winding), so the inward normal points to the right of the direction of travel `(x2-x1, y2-y1)`:
```
nx =  (y2 - y1) / length
ny = -(x2 - x1) / length
```

---

## 7. Polygon Definitions — `makeWalls2D()`

Add a new static function `makeWalls2D()` to `IRSynthEngine.cpp`:

```cpp
static std::vector<Wall2D> makeWalls2D(
    const IRSynthParams& p,
    double W, double D,
    const double rW[N_BANDS])
```

This function generates the wall list for the given shape and actual dimensions. **For `"Rectangular"` this function is never called** — the existing `calcRefs` handles it.

### Fan / Shoebox

4 walls. Taper `t = p.shapeTaper ∈ [0, 0.70]`.

Stage end (y=0) is narrower, audience end (y=D) is full width W. The front wall is centred:

```
stage_left  = (W * t/2,       0)
stage_right = (W * (1 - t/2), 0)
back_left   = (0,              D)
back_right  = (W,              D)
```

Vertices in counter-clockwise order: `stage_left → back_left → back_right → stage_right → stage_left`

Walls:
| Wall | From | To | Description |
|---|---|---|---|
| 0 | `stage_left` | `back_left` | Left angled wall |
| 1 | `back_left` | `back_right` | Back wall (full width) |
| 2 | `back_right` | `stage_right` | Right angled wall |
| 3 | `stage_right` | `stage_left` | Stage (front) wall (narrow) |

At `t=0`: front corners at `(0,0)` and `(W,0)` → rectangle (but `calcRefsPolygon` will still be called; at t=0 it should produce identical results to `calcRefs`. This is worth verifying in a test.)

**Maximum reflection order for Fan/Shoebox:** Use the RT60-based formula with a cap of 20:
```cpp
int mo_fan = std::min(20, (int)std::floor(rt60_mf * SPEED / minDim / 2.0));
```
Rationale: Only 4 horizontal walls. The combinatorial growth `4^order` is manageable with path-length early termination even at order 20.

---

### Octagonal

8 walls. Corner cut `c = p.shapeCornerCut ∈ [0, 1]`.

Corner cut distance: `cc = c × min(W, D) × 0.293`  
(At c=1 with W=D: cc = W×0.293, producing a regular octagon where all 8 sides are equal.)

```
Vertices (8, counter-clockwise from bottom-left):
P0 = (cc,   0)
P1 = (W-cc, 0)
P2 = (W,    cc)
P3 = (W,    D-cc)
P4 = (W-cc, D)
P5 = (cc,   D)
P6 = (0,    D-cc)
P7 = (0,    cc)
```

8 walls: `P0→P1` (bottom), `P1→P2` (bottom-right chamfer), `P2→P3` (right), `P3→P4` (top-right chamfer), `P4→P5` (top), `P5→P6` (top-left chamfer), `P6→P7` (left), `P7→P0` (bottom-left chamfer).

At `c=0`: `cc=0`, collapses to 4-vertex rectangle. When this happens, `calcRefsPolygon` will still be called but should give results very close to `calcRefs` (degenerate walls of zero length should be handled gracefully — zero-length walls should be skipped in `makeWalls2D`).

**Maximum reflection order:** cap at 8.

---

### Circular Hall (16-sided polygon)

16 walls. Corner cut `c = p.shapeCornerCut ∈ [0, 1]`. Same parameter as Octagonal; controls how "round" vs "boxy" the hall is.

Vertices: 16 points interpolated between the bounding rectangle (c=0) and an ellipse inscribed in the bounding box (c=1).

For each vertex `i ∈ [0,15]`:
```cpp
double base_angle = i * 2.0 * M_PI / 16.0 + M_PI / 16.0; // aligned so flat sides face axes

// On the inscribed ellipse (c=1):
double ex = W/2.0 + (W/2.0) * std::cos(base_angle);
double ey = D/2.0 + (D/2.0) * std::sin(base_angle);

// On the bounding rectangle (c=0):
// Project the angle onto the rectangle boundary
double rx, ry;
{
    double ca = std::cos(base_angle), sa = std::sin(base_angle);
    double sx_scale = (std::abs(ca) > 1e-9) ? (W/2.0 / std::abs(ca)) : 1e18;
    double sy_scale = (std::abs(sa) > 1e-9) ? (D/2.0 / std::abs(sa)) : 1e18;
    double scale = std::min(sx_scale, sy_scale);
    rx = W/2.0 + scale * ca;
    ry = D/2.0 + scale * sa;
}

// Interpolate:
double vx = rx + c * (ex - rx);
double vy = ry + c * (ey - ry);
```

This gives a smooth family from rectangle (c=0) through 16-gon (c=1). The visual shape in FloorPlanComponent should use the same formula for consistency.

16 walls connecting consecutive vertices counter-clockwise.

**Maximum reflection order:** cap at 6.

---

### Cathedral (cruciform)

12 walls. Nave percent `nw = p.shapeNavePct ∈ [0.15, 0.50]`, transept percent `td = p.shapeTrptPct ∈ [0.20, 0.45]`.

```
hw  = W * nw / 2.0    // half-width of nave (metres from centre to nave wall)
ht  = D * td / 2.0    // half-depth of transept arm (metres from centre to transept end wall)
cx  = W / 2.0
cy  = D / 2.0
```

Clamp to prevent degenerate geometry:
- Transept arms must not extend outside bounding box: `ht ≤ cy - 0.5` (0.5m minimum nave at each end)
- Nave must not be wider than transepts: `hw ≤ W/2.0 - 0.5` (minimum 0.5m transept wall visible)

12 vertices counter-clockwise (starting top-left of nave, going clockwise around the exterior):
```
P0  = (cx - hw,  0    )   // top of nave, left
P1  = (cx + hw,  0    )   // top of nave, right
P2  = (cx + hw,  cy - ht) // inner corner: nave/right-transept junction top
P3  = (W,        cy - ht) // right transept outer wall, top
P4  = (W,        cy + ht) // right transept outer wall, bottom
P5  = (cx + hw,  cy + ht) // inner corner: nave/right-transept junction bottom
P6  = (cx + hw,  D    )   // bottom of nave, right
P7  = (cx - hw,  D    )   // bottom of nave, left
P8  = (cx - hw,  cy + ht) // inner corner: nave/left-transept junction bottom
P9  = (0,        cy + ht) // left transept outer wall, bottom
P10 = (0,        cy - ht) // left transept outer wall, top
P11 = (cx - hw,  cy - ht) // inner corner: nave/left-transept junction top
```

12 walls: `P0→P1` (top nave), `P1→P2` (right nave wall, top section), `P2→P3` (top transept step, right), `P3→P4` (right outer transept wall), `P4→P5` (bottom transept step, right), `P5→P6` (right nave wall, bottom section), `P6→P7` (bottom nave), `P7→P8` (left nave wall, bottom section), `P8→P9` (bottom transept step, left), `P9→P10` (left outer transept wall), `P10→P11` (top transept step, left), `P11→P0` (left nave wall, top section).

**Maximum reflection order:** cap at 6.

---

## 8. Core Algorithm: `calcRefsPolygon()`

### Geometric utility functions

Add these as file-static functions in `IRSynthEngine.cpp`:

#### `reflect2D`
```cpp
static std::pair<double,double> reflect2D(double px, double py, const Wall2D& w) noexcept
{
    double dx = w.x2 - w.x1, dy = w.y2 - w.y1;
    double len2 = dx*dx + dy*dy;
    if (len2 < 1e-18) return {px, py};  // degenerate wall — return unchanged
    double t = ((px - w.x1)*dx + (py - w.y1)*dy) / len2;
    double fx = w.x1 + t*dx;
    double fy = w.y1 + t*dy;
    return { 2.0*fx - px, 2.0*fy - py };
}
```

#### `rayIntersectsSegment`
Returns `true` if the ray from `(ax,ay)` to `(bx,by)` crosses the finite wall segment, with intersection parameters `t ∈ [0,1]` along the ray and `s ∈ [0,1]` along the wall. Uses a small epsilon for robustness at near-grazing angles.

```cpp
static bool rayIntersectsSegment(
    double ax, double ay, double bx, double by,
    const Wall2D& w,
    double& t_out, double& s_out) noexcept
{
    double dx = bx - ax, dy = by - ay;
    double wx = w.x2 - w.x1, wy = w.y2 - w.y1;
    double denom = dx * wy - dy * wx;
    if (std::abs(denom) < 1e-12) return false;  // parallel
    double t = ((w.x1 - ax) * wy - (w.y1 - ay) * wx) / denom;
    double s = ((w.x1 - ax) * dy  - (w.y1 - ay) * dx) / denom;
    t_out = t;
    s_out = s;
    constexpr double EPS = 1e-9;
    return (t > EPS && t < 1.0 + EPS && s >= -EPS && s <= 1.0 + EPS);
}
```

#### `polygonArea` and `polygonPerimeter`
Used in the corrected `calcRT60` and `renderFDNTail`:

```cpp
// Shoelace formula — works for any simple (non-self-intersecting) polygon
static double polygonArea(const std::vector<Wall2D>& walls) noexcept
{
    double area = 0.0;
    for (const auto& w : walls)
        area += w.x1 * w.y2 - w.x2 * w.y1;
    return std::abs(area) * 0.5;
}

static double polygonPerimeter(const std::vector<Wall2D>& walls) noexcept
{
    double perim = 0.0;
    for (const auto& w : walls)
        perim += std::sqrt((w.x2-w.x1)*(w.x2-w.x1) + (w.y2-w.y1)*(w.y2-w.y1));
    return perim;
}
```

---

### `ImageSource2D` struct

Internal to `IRSynthEngine.cpp` (file-static, not in header):

```cpp
struct ImageSource2D {
    double x, y;                     // 2D image source position (actual metres)
    std::vector<int> wallPath;        // sequence of wall indices hit (oldest first)
    double cumAbs[N_BANDS];           // accumulated reflection product (product of rAbs per wall hit)
    int    order;                     // = wallPath.size()
};
```

---

### `generateIS2D()` — recursive image-source tree

File-static function in `IRSynthEngine.cpp`. This is the core of the Borish algorithm.

```cpp
static void generateIS2D(
    const std::vector<Wall2D>& walls,
    double imgX, double imgY,
    const std::vector<int>& wallPath,
    const double cumAbs[N_BANDS],
    double rcvX, double rcvY,
    double maxDist2D,    // early-terminate: 2D distance img→rcv > this → skip
    int maxOrder,
    std::vector<ImageSource2D>& out)
{
    // Horizontal distance from image source to receiver
    double d2D = std::hypot(imgX - rcvX, imgY - rcvY);
    if (d2D > maxDist2D) return;  // cannot arrive in time (will only get further with more bounces)

    // Accept this image source
    ImageSource2D is;
    is.x = imgX; is.y = imgY;
    is.wallPath = wallPath;
    is.order = (int)wallPath.size();
    std::copy(cumAbs, cumAbs + N_BANDS, is.cumAbs);
    out.push_back(std::move(is));

    if ((int)wallPath.size() >= maxOrder) return;

    // Generate child image sources by reflecting through each wall
    for (int wi = 0; wi < (int)walls.size(); ++wi) {
        // Prevent consecutive reflection through the same wall
        if (!wallPath.empty() && wallPath.back() == wi) continue;

        const Wall2D& w = walls[wi];

        // The image source must be on the OUTSIDE of this wall (behind it relative to room interior).
        // Check: image source is on the side the wall normal points away from.
        // dot(imgPos - wallPoint, inwardNormal) should be < 0 for a valid image source.
        double dot = (imgX - w.x1) * w.nx + (imgY - w.y1) * w.ny;
        if (dot > -1e-9) continue;  // image source is inside the room — invalid reflection

        // Reflect
        auto [newX, newY] = reflect2D(imgX, imgY, w);

        // Validate visibility: ray from new image source to receiver must intersect this wall
        // within the segment bounds
        double t_param, s_param;
        if (!rayIntersectsSegment(newX, newY, rcvX, rcvY, w, t_param, s_param))
            continue;

        // For order > 1: validate that the ray from this new image source to the PREVIOUS
        // image source intersection point also passes through the correct intermediate wall.
        // Full path validation is done here for robustness at higher orders.
        // For orders 1-2 the visibility test above is sufficient; for order 3+ implement
        // full chain validation (see implementation note below).
        //
        // Implementation note: for the initial implementation, the single-step visibility
        // test is sufficient for orders 1-3. At order 4+ false image sources can slip through
        // without full chain validation, but with path-length pruning the impact is small.
        // Full chain validation can be added in a follow-up pass.

        // Accumulate absorption
        double newAbs[N_BANDS];
        for (int b = 0; b < N_BANDS; ++b)
            newAbs[b] = cumAbs[b] * w.rAbs[b];

        // Recurse
        std::vector<int> newPath = wallPath;
        newPath.push_back(wi);
        generateIS2D(walls, newX, newY, newPath, newAbs,
                     rcvX, rcvY, maxDist2D, maxOrder, out);
    }
}
```

**Implementation note on full chain validation:** For the highest correctness (particularly at orders 4–6), each image source in the tree requires that the entire path `newImgSrc → wall[n] → prev_img → wall[n-1] → ... → wall[0] → receiver` is geometrically valid. The single-step test above (`newX,newY → rcvX,rcvY` must cross wall `wi`) is correct for order 1. At orders 2+, there can be false acceptances. The impact is bounded because: (a) path-length early termination filters most false candidates, and (b) false image sources at high order are weak (heavily attenuated). Full chain validation is recommended for a follow-up but the single-step approach is acceptable for the initial implementation.

---

### `calcRefsPolygon()` — main function

This is a direct parallel to `calcRefs()`. It should accept the same parameter signature where possible so `synthMainPath` / `synthDirectPath` can route between them with minimal duplication.

```cpp
static std::vector<Ref> calcRefsPolygon(
    const IRSynthParams& p,
    double sx, double sy, double sz,
    double rx, double ry, double rz,
    double W, double D, double He,
    const double rW[N_BANDS],
    const double rF[N_BANDS],
    const double rC[N_BANDS],
    double ts,        // diffusion (same as calcRefs)
    double vHfA,      // vault HF absorption (same as calcRefs)
    bool eo,          // ER-only mode
    double ec,        // ER cutoff (samples)
    int sr,
    int irLen,
    double minJitterMs,
    double highOrderJitterMs,
    bool lambertScatter,
    bool spkDirFull,
    uint32_t seed)
{
    std::vector<Ref> refs;

    // 1. Build polygon walls
    auto walls = makeWalls2D(p, W, D, rW);
    // makeWalls2D sets wall.rAbs[b] = rW[b] for all side walls (same material initially)

    // 2. Determine horizontal max order and max 2D path distance
    double mfp = 4.0 * polygonArea(walls) * He /
                 (2.0 * polygonArea(walls) + polygonPerimeter(walls) * He);
    double rt60_mf = /* use calcRT60 result — pass in or recompute */ 0.0;
    // (rt60_mf should be passed in from synthMainPath, where calcRT60 has already run)

    int mo_horiz = orderLimitForShape(p.shape, rt60_mf, W, D);
    // See section 9 for orderLimitForShape()

    // maxDist2D: horizontal path budget
    // ER window = ec/sr seconds = ec/sr * SPEED metres
    // Add 20% headroom for receiver-not-at-origin geometry
    double ecDist3D = (ec / (double)sr) * SPEED * 1.2;
    double maxDist2D = ecDist3D;  // conservative — 2D distance ≤ 3D distance

    // 3. Generate 2D image source tree
    std::vector<ImageSource2D> imageSources;
    imageSources.reserve(4096);

    double initAbs[N_BANDS];
    std::fill(initAbs, initAbs + N_BANDS, 1.0);

    // Direct source (order 0) — one "image source" at the actual source position
    // (Note: this must pass the polygon containment check — the source IS inside the room)
    generateIS2D(walls, sx, sy, {}, initAbs, rx, ry, maxDist2D, mo_horiz, imageSources);

    // 4. nz loop (floor/ceiling — identical to calcRefs)
    // Compute nz_max from RT60 and vertical dimension
    int nz_max = std::min(30, (int)std::floor(rt60_mf * SPEED / (2.0 * He)));

    for (int nz = -nz_max; nz <= nz_max; ++nz) {
        double iz = nz * He + (nz % 2 ? He - sz : sz);

        for (const auto& is : imageSources) {
            double dx = is.x - rx, dy = is.y - ry, dz = iz - rz;
            double dist = std::sqrt(dx*dx + dy*dy + dz*dz);
            if (dist < 1e-6) continue;

            double t_sec = dist / SPEED;
            int    bt    = (int)std::round(t_sec * sr);

            if (bt < 0 || bt >= irLen) continue;
            if (eo && t_sec >= ec / (double)sr) continue;  // ER-only gate

            // Base amplitude (1/r law, same as calcRefs)
            double a = 1.0 / std::max(dist, 0.5);

            double amps[N_BANDS];
            for (int b = 0; b < N_BANDS; ++b) {
                double ab = a;

                // Air absorption (same as calcRefs)
                ab *= std::pow(10.0, -AIR[b] * dist / 20.0);

                // Polygon wall absorption (from accumulated product)
                ab *= is.cumAbs[b];

                // Floor/ceiling absorption (same as calcRefs)
                ab *= std::pow(rF[b], std::ceil(std::abs(nz) / 2.0));
                ab *= std::pow(rC[b], std::floor(std::abs(nz) / 2.0));

                // Vault HF absorption per ceiling bounce (same as calcRefs)
                if (std::abs(nz) > 0)
                    ab *= std::pow(1.0 - vHfA * std::min(b / 3.0, 1.0), std::abs(nz));

                amps[b] = ab;
            }

            // Speaker directivity (same logic as calcRefs — angle from actual source, not image)
            int totalBounces = is.order + std::abs(nz);
            // spkG calculation: use actual source-to-receiver angle for order 0,
            // use image source position for higher orders (same as calcRefs approach)
            // ... (copy from calcRefs, substituting is.x/is.y for ix/iy)

            // Mic polar pattern (same as calcRefs — 3D directivityCos)
            // ... (copy from calcRefs, substituting is.x/is.y/iz for ix/iy/iz)

            // Time jitter (same as calcRefs — apply to totalBounces)
            // ...

            // Lambert scatter Feature A (same logic as calcRefs order 1-3)
            // Only apply to reflections where is.order + |nz| is in {1,2,3}
            // and ts > 0.05. Scatter arrivals subject to ER-only gate.
            // ...

            Ref r;
            r.t    = bt;
            r.seed = seed;  // for determinism
            std::copy(amps, amps + N_BANDS, r.amps);
            r.order = totalBounces;
            refs.push_back(r);
        }
    }

    return refs;
}
```

**Important:** the `...` sections above should be copy-adapted from the corresponding sections of `calcRefs()`. The logic for speaker directivity, mic polar pattern, time jitter, and Lambert scatter is geometrically identical — only the source of `(ix, iy, iz)` changes (now from `is.x, is.y, iz` instead of the Cartesian mirror formula).

---

## 9. Reflection Order Limits per Shape

Add this helper function to `IRSynthEngine.cpp`:

```cpp
static int orderLimitForShape(
    const std::string& shape,
    double rt60_mf,
    double W, double D) noexcept
{
    double minDim = std::min(W, D);
    // RT60-based order (same formula as calcRefs, but we cap per-shape)
    int mo_rt60 = (int)std::floor(rt60_mf * SPEED / minDim / 2.0);

    if (shape == "Fan / Shoebox")
        return std::min(20, mo_rt60);   // 4 walls — tractable at high order

    if (shape == "Octagonal")
        return std::min(8, mo_rt60);

    if (shape == "Circular Hall")
        return std::min(6, mo_rt60);    // 16 walls — keep order low

    if (shape == "Cathedral")
        return std::min(6, mo_rt60);    // 12 walls — keep order low

    return std::min(60, mo_rt60);       // Rectangular fallback (shouldn't reach here)
}
```

**Rationale for caps:**

| Shape | Walls | Hard cap | Rationale |
|---|---|---|---|
| Fan/Shoebox | 4 horizontal | 20 | Minimal walls; with path-length pruning this is fast |
| Octagonal | 8 horizontal | 8 | 8^8 ~16M candidates naively; ~50K after path-pruning |
| Circular Hall | 16 horizontal | 6 | 16^6 ~16M candidates naively; ~80K after pruning |
| Cathedral | 12 horizontal | 6 | 12^6 ~3M candidates naively; ~80K after pruning |

Path-length early termination in `generateIS2D` means the actual number of candidates visited is far lower than the naive `N_walls^order` figure, especially for large rooms where most paths exceed the ER window by order 3-4.

---

## 10. Corrected Supporting Calculations

### `calcRT60()` — polygon surface area

> ⚠️ **Rectangular must use the existing formula verbatim — not polygon math that "happens to give the same answer".** Use the hard gate below. This eliminates any risk of floating-point drift and makes it impossible for a refactor to accidentally affect the rectangular path. The existing tests (IR_01–IR_26) all use `shape = "Rectangular"` and must remain bit-identical.

Currently: `fA = p.width × p.depth`, `sA = 2 × depth × He`, `eA = 2 × width × He`.

Replace with:

```cpp
double polyArea, polyPerim;
if (p.shape == "Rectangular") {
    // EXISTING formula — do not change, do not replace with polygon math
    polyArea  = p.width * p.depth;
    polyPerim = 2.0 * (p.width + p.depth);
} else {
    // Polygon shapes: compute actual footprint area and wall perimeter
    auto walls2D = makeWalls2D(p, W, D, rW_dummy);
    polyArea  = polygonArea(walls2D);
    polyPerim = polygonPerimeter(walls2D);
}

double fA = polyArea;                              // floor
double cA = polyArea * (1.0 + (hm - 1.0) * 1.6); // ceiling (vault scaling unchanged)
double wA = polyPerim * He;                        // all side walls
double tS = fA + cA + wA;

double vol = polyArea * H;
```

**Side wall absorption:** currently uses a weighted blend of wall and window materials applied to `sA + eA`. For polygon shapes, all side walls use the same blend applied to `wA = polyPerim × He`. This is equivalent.

---

### `renderFDNTail()` — mean free path

> ⚠️ **Same hard gate required as `calcRT60`.** `"Rectangular"` must use the existing formula exactly.

Currently: `vol = roomW × roomD × roomH`, `surf = 2(WD + DH + WH)`.

Replace with:

```cpp
double polyArea, polyPerim;
if (p.shape == "Rectangular") {
    // EXISTING formula — do not replace with polygon math
    polyArea  = roomW * roomD;
    polyPerim = 2.0 * (roomW + roomD);
} else {
    auto walls2D = makeWalls2D(p, roomW, roomD, rW_dummy);
    polyArea  = polygonArea(walls2D);
    polyPerim = polygonPerimeter(walls2D);
}

double vol  = polyArea * roomH;
double surf = 2.0 * polyArea + polyPerim * roomH;
double mfp  = 4.0 * vol / surf;
```

For `"Rectangular"`: `polyArea = roomW × roomD`, `polyPerim = 2(roomW + roomD)` → `vol` and `surf` are numerically identical to the current formula. The gate ensures this is the *same code path*, not coincidentally equivalent code.

---

### `applyModalBank()` — disable for non-rectangular shapes

Axial modes (`f_n = c×n / (2×L)`) are only physically meaningful for rectangular rooms. For all non-rectangular shapes, return the buffer unchanged:

```cpp
std::vector<double> IRSynthEngine::applyModalBank(
    const std::vector<double>& buf,
    double W, double D, double He,
    double rt60_125, double gain, int sr,
    const std::string& shape)   // ADD THIS PARAMETER
{
    if (shape != "Rectangular")
        return buf;  // modal bank not applicable to non-rectangular rooms
    
    // ... existing rectangular implementation unchanged ...
}
```

Update the call site in `synthMainPath` to pass `p.shape`.

---

## 11. Routing in `synthMainPath`, `synthDirectPath`, `synthExtraPath`

In each of the three synth helpers, `calcRefs` is called (currently unconditionally). Replace with a dispatch:

```cpp
// BEFORE (current):
auto eLL = calcRefs(p, slx, sly, sz, rlx, rly, rz, W, D, He, ..., seed+0);
auto eRL = calcRefs(p, srx, sry, sz, rlx, rly, rz, W, D, He, ..., seed+1);
// etc.

// AFTER:
bool usePolygon = (p.shape != "Rectangular");

auto calcRefsDispatch = [&](double sx, double sy, double sz,
                             double rx, double ry, double rz,
                             uint32_t s) -> std::vector<Ref>
{
    if (usePolygon)
        return calcRefsPolygon(p, sx, sy, sz, rx, ry, rz, W, D, He, ...);
    else
        return calcRefs(p, sx, sy, sz, rx, ry, rz, W, D, He, ...);
};

auto eLL = calcRefsDispatch(slx, sly, sz, rlx, rly, rz, seed+0);
auto eRL = calcRefsDispatch(srx, sry, sz, rlx, rly, rz, seed+1);
// etc.
```

**The lambda wrapper isolates the dispatch to a single point** and makes it impossible to accidentally call the wrong variant. Use it in all three synth helpers.

**The rectangular golden values (IR_11, IR_14) are protected** because `p.shape == "Rectangular"` always routes through the original `calcRefs()`.

---

## 12. UI Changes

### `IRSynthComponent` — shape proportion sliders

Add four new `juce::Slider` members and matching `juce::AudioProcessorValueTreeState::SliderAttachment` replacements (since these are `IRSynthParams` fields, not APVTS — they wire through `onParamModifiedFn` via `slider.onValueChange`):

```cpp
// Members (add to IRSynthComponent.h):
juce::Slider navePctSlider;     // Cathedral only
juce::Slider trptPctSlider;     // Cathedral only
juce::Slider taperSlider;       // Fan/Shoebox only
juce::Slider cornerCutSlider;   // Octagonal and Circular Hall only
juce::Label  navePctLabel, trptPctLabel, taperLabel, cornerCutLabel;
```

These sliders are shown/hidden conditionally in `resized()` / `paint()` based on the currently selected shape:

| Shape | Visible sliders |
|---|---|
| Rectangular | None |
| Fan / Shoebox | Taper |
| Octagonal | Corner Cut |
| Circular Hall | Corner Cut |
| Cathedral | Nave Width %, Transept Depth % |

Place the proportion sliders in a new sub-row below the Width/Depth/Height sliders in the Room Geometry section of the left panel. They should follow the same single-row label+slider+readout layout as the Width/Depth/Height sliders.

**Slider ranges and readout formats:**

| Slider | Range | Step | Readout |
|---|---|---|---|
| Nave Width % | 15–50% | 1% | `"30%"` |
| Transept Depth % | 20–45% | 1% | `"35%"` |
| Taper | 0–70% | 1% | `"30%"` |
| Corner Cut | 0–100% | 1% | `"50%"` |

**`setParams` / `getParams`:** Read/write `p.shapeNavePct`, `p.shapeTrptPct`, `p.shapeTaper`, `p.shapeCornerCut` directly. Apply the same `suppressingParamNotifications` guard as all other slider updates.

### `IRSynthComponent` — shape combo changes

Update `shapeCombo` items:
- Remove `"L-shaped"`
- Rename `"Cylindrical"` to `"Circular Hall"`

The combo now contains: Rectangular, Fan / Shoebox, Octagonal, Circular Hall, Cathedral.

When `shapeCombo` changes, call the existing `onParamModifiedFn` and additionally call `updateShapeControlVisibility()` which shows/hides the proportion sliders.

### `FloorPlanComponent` — parametric `roomPoly()`

Change `roomPoly(const std::string& shape)` to `roomPoly(const IRSynthParams& p)` (or add an overload that takes the full params). Update the shape polygon functions to be parametric:

- `"Cathedral"`: use `p.shapeNavePct` and `p.shapeTrptPct` in the vertex formula (section 7)
- `"Fan / Shoebox"`: use `p.shapeTaper`
- `"Octagonal"`: use `p.shapeCornerCut`
- `"Circular Hall"`: use `p.shapeCornerCut` in the ellipse-interpolation formula (section 7)
- Remove the `"L-shaped"` case
- Replace the old `"Cylindrical"` case with `"Circular Hall"`

`isInsideRoom()` should also take `IRSynthParams` so it uses the same parametric polygon.

**FloorPlanComponent must also call `buildParamsFromState()` / `getParams()` to access the proportion values** — they must be stored in `TransducerState` or read from `IRSynthComponent` via a callback. The cleanest approach: pass the full current `IRSynthParams` to `roomPoly()` from wherever `paint()` already accesses params.

---

## 13. Phased Implementation Order

Implement in this sequence to allow incremental validation at each phase:

### Phase 1 — Infrastructure (no acoustic changes yet)

1. Add `shapeNavePct`, `shapeTrptPct`, `shapeTaper`, `shapeCornerCut` to `IRSynthParams` with defaults.
2. Add `Wall2D` struct to `IRSynthEngine.h`.
3. Add `polygonArea()`, `polygonPerimeter()`, `reflect2D()`, `rayIntersectsSegment()` as file-static functions in `IRSynthEngine.cpp` (no callers yet — just getting them in place and testable).
4. Add `makeWalls2D()` for all four polygon shapes.
5. Add sidecar read/write for the four new params in `PluginProcessor.cpp`.
6. Add shape string migration in `PluginProcessor.cpp` ("Cylindrical" → "Circular Hall", "L-shaped" → "Rectangular").
7. Update `IRSynthComponent`: add proportion sliders, conditional visibility, combo rename/removal.
8. Update `FloorPlanComponent`: parametric `roomPoly()`, rename Cylindrical → Circular Hall visual, remove L-shaped, update `isInsideRoom()`.

**Checkpoint:** Build compiles, UI shows correct sliders per shape, floor plan draws correct parametric polygon, old sidecars load correctly.

### Phase 2 — Corrected supporting math

1. Update `calcRT60()` to use `polygonArea()` / `polygonPerimeter()` for non-rectangular shapes. Gate behind `if (p.shape != "Rectangular")`.
2. Update `renderFDNTail()` volume/surface area for non-rectangular shapes. Same gate.
3. Add `shape` parameter to `applyModalBank()` and add the early-return for non-rectangular.

**Checkpoint:** Synthesise a Cathedral and Octagonal IR. The RT60 should change slightly (more accurate surface area). The tail character improves. Rectangular synthesis is bit-identical to before. Run full test suite — all existing tests pass.

### Phase 3 — Core polygon ISM

1. Implement `ImageSource2D` struct (file-static in `IRSynthEngine.cpp`).
2. Implement `generateIS2D()` recursive function.
3. Implement `orderLimitForShape()`.
4. Implement `calcRefsPolygon()` using the components above.
5. Add the dispatch lambda to `synthMainPath` (only). Verify thoroughly before touching `synthDirectPath` / `synthExtraPath`.
6. Add dispatch to `synthDirectPath`.
7. Add dispatch to `synthExtraPath`.

**Checkpoint:** Synthesise all four polygon shapes. Listen for:
- Cathedral: distinct transept echo / lateral energy not present in rectangular version ✓
- Octagonal: 8-directional early density ✓
- Circular Hall: dense omni-directional early field ✓
- Fan/Shoebox: asymmetric stage-end / back-of-hall reflections ✓

Rectangular synthesis: run `./PingTests` — IR_11, IR_14 golden values unchanged ✓.

### Phase 4 — Tests

Write `Tests/PingPolygonTests.cpp` (see section 14). All new tests IR_27–IR_32, DSP_22 must pass.

### Phase 5 — Tuning, performance profiling, factory IR regeneration

1. Measure synthesis time per shape across small/medium/large rooms. Adjust order caps if needed.
2. Run `./build/generate_factory_irs` to regenerate factory IRs for any venues that used non-rectangular shapes. Identify which factory IRs used "Cylindrical" or "Cathedral" or "Octagonal" — they need new sidecars with updated `shape` strings and proportion params.
3. Update `CLAUDE.md` (see section 16).
4. Update `generate_factory_irs.cpp` to use the new `IRSynthParams` shape fields.

---

## 14. Test Plan — `Tests/PingPolygonTests.cpp`

### DSP_22 — Geometry utility unit tests

Tests `reflect2D`, `rayIntersectsSegment`, `polygonArea`, `polygonPerimeter` directly.

```cpp
TEST_CASE("DSP_22: 2D polygon geometry utilities", "[DSP_22]")
{
    // reflect2D: point reflected through vertical wall at x=5
    Wall2D vwall = {5,0, 5,10, 1,0, {/* abs */}};
    auto [rx, ry] = reflect2D(3.0, 4.0, vwall);
    REQUIRE(std::abs(rx - 7.0) < 1e-9);   // reflected across x=5
    REQUIRE(std::abs(ry - 4.0) < 1e-9);   // y unchanged

    // reflect2D: point reflected through 45° wall
    Wall2D dwall = {0,0, 10,10, ...};
    // ... check that (3,0) reflects to (0,3) etc.

    // rayIntersectsSegment: ray should hit
    Wall2D w = {5,0, 5,10, ...};
    double t, s;
    bool hit = rayIntersectsSegment(0,5, 10,5, w, t, s);
    REQUIRE(hit);
    REQUIRE(std::abs(t - 0.5) < 1e-9);
    REQUIRE(std::abs(s - 0.5) < 1e-9);

    // rayIntersectsSegment: parallel ray should miss
    hit = rayIntersectsSegment(0,5, 0,10, w, t, s);
    REQUIRE(!hit);

    // polygonArea: unit square
    std::vector<Wall2D> square = {{0,0,1,0,{}}, {1,0,1,1,{}}, {1,1,0,1,{}}, {0,1,0,0,{}}};
    REQUIRE(std::abs(polygonArea(square) - 1.0) < 1e-9);

    // polygonArea: known 5×10 rectangle
    // polygonPerimeter: known 5×10 rectangle → perimeter = 30
}
```

---

### IR_27 — Fan/Shoebox angled wall arrival time

With `shapeTaper = 0.5` and room `W=20, D=30`:
- Source at stage centre `(0.5, 0.0)` (normalised) — actual position `(10, 0.5)` (on the stage wall, 0.5m inside)
- Receiver at `(0.5, 0.8)` — actual `(10, 24)`
- The angled side wall at `x ≈ W * t/4 = 2.5m` at the stage end should produce a first-order reflection
- Verify arrival time is consistent with the actual wall geometry (compute expected distance analytically)
- Crucially: compare to `shapeTaper=0` (rectangular) — the arrival time **must differ** by at least 5ms to confirm the geometry is real, not a scalar approximation

---

### IR_28 — Octagonal first-order distinctness

With an octagonal room and `shapeCornerCut=0.5`:
- Place source and receiver near the centre
- Extract all first-order reflections (arrivals within 30ms for a small room)
- Count distinct arrival times: should be 8 (one per wall) rather than 4 (as in a rectangle where opposite walls are equidistant)
- This test confirms the 8-wall geometry is genuinely computing 8 independent wall paths

---

### IR_29 — Cathedral transept reflections present

Default Cathedral params (`shapeNavePct=0.30`, `shapeTrptPct=0.35`), room `W=28, D=60`.
- Source at `(cx, cy-2)` (just north of crossing), receiver at `(cx, cy+2)` (just south of crossing)
- Transept outer walls at `x=0` and `x=W` in the transept zone
- Expected first-order transept reflection path: source → left/right outer wall → receiver
  - Distance ≈ 2 × `(cx - hw)` where `hw = W × nw / 2 = 28×0.15 = 4.2m`, so `cx - hw = 14 - 4.2 = 9.8m`
  - Approx path: receiver-to-wall ≈ 9.8m, total first-order ≈ ~20m → arrival ~58ms
- Verify that an energy arrival exists in the [50ms, 70ms] window
- Compare to same room as "Rectangular": NO corresponding arrival should exist at those times
- This is the key "shape actually works" test for Cathedral

---

### IR_30 — Circular Hall 16-wall independence

With `shapeCornerCut=1.0` (full 16-gon), small room `W=D=20m`:
- Count distinct first-order arrival times: should be ~16 (one per wall face, some may be nearly coincident due to symmetry with W=D)
- With W≠D (e.g. W=20, D=25): all 16 arrivals should be at distinct times

---

### IR_31 — Polygon area/perimeter accuracy

Unit test `makeWalls2D` for each shape at known dimensions and verify `polygonArea` and `polygonPerimeter` match hand-calculated values:
- Fan/Shoebox `W=20, D=30, t=0.5`: area = `(stage_width + back_width)/2 × D = (10+20)/2 × 30 = 450 m²`
- Cathedral `W=28, D=60, nw=0.30, td=0.35`: area = total_rect - 4×corner_cutouts = `28×60 - 4×((W/2-hw)×(D/2-ht))` = `1680 - 4×(9.8×18.5) = 1680 - 725.2 = 954.8 m²`
- Rectangular `W=28, D=60`: area = 1680 m²

---

### IR_32 — Rectangular regression lock

With default room params and `shape="Rectangular"`:
- Synthesise a full IR
- Verify bit-identity with the pre-change engine output
- This is the most important regression guard — it confirms the dispatch correctly routes `"Rectangular"` to the original `calcRefs` path

Note: this may need to be implemented as a comparison against a stored baseline rather than a hash, since the baseline is whatever the current engine produces. Run the current engine first, capture 30 samples (same approach as IR_11), lock them, then verify post-change.

Alternatively: if IR_11 and IR_14 already fully cover the rectangular path, IR_32 may be redundant. Add it only if IR_11/IR_14 feel insufficient.

---

## 15. Performance Estimates

These are rough estimates for synthesis time **increase** relative to the current rectangular engine for a typical large room (28×60×12m, RT60 ~4s).

| Shape | Horizontal walls | Hard order cap | Estimated ER calc multiplier | Expected total synthesis time delta |
|---|---|---|---|---|
| Fan/Shoebox | 4 | 20 | ~1.5–3× | +1–3s |
| Octagonal | 8 | 8 | ~3–8× | +2–6s |
| Circular Hall | 16 | 6 | ~5–15× | +4–12s |
| Cathedral | 12 | 6 | ~5–15× | +4–12s |

**Important caveat:** The ER calculation (which `calcRefsPolygon` replaces) is **one part** of the total synthesis time. `renderFDNTail`, `renderCh` (bandpass filtering), and post-processing are also significant. The overall synthesis time increase is typically smaller than the ER-only multiplier suggests.

**Path-length early termination is the key performance lever.** For large rooms, most candidate image sources exceed the 85ms ER window by order 3-4, so `generateIS2D` terminates branches very quickly. Small rooms (< 10m sides) will see the worst-case performance since more paths remain within the ER window.

The order caps in `orderLimitForShape()` are initial conservative values. After Phase 5 profiling they may be tuned up or down.

---

## 16. Key Invariants and Guard Rails for the Implementer

Read these before touching any code:

1. **`calcRefs()` is sacred.** Never modify it. Never route "Rectangular" through `calcRefsPolygon`. The dispatch lambda must have a hard `if (p.shape == "Rectangular") return calcRefs(...)` path with no exceptions.

2. **`calcRT60`, `renderFDNTail`, and `applyModalBank` must use hard `if (p.shape == "Rectangular")` gates** — not "polygon math that happens to give the same answer for a rectangle." Use the existing rectangular formula verbatim in the `true` branch. Do not call `makeWalls2D`, `polygonArea`, or `polygonPerimeter` when `shape == "Rectangular"`. This is not a stylistic preference — it is the guarantee that all existing tests (IR_01–IR_26, DSP_01–DSP_21) remain bit-identical after the change. Any approach that routes Rectangular through the polygon code path, even if mathematically equivalent, risks floating-point drift and makes it impossible to reason about regressions.

3. **The `nz` floor/ceiling loop is correct for all shapes.** All polygon shapes have flat floors and ceilings. The `nz` absorption formula `pow(rF, ceil(|nz|/2)) × pow(rC, floor(|nz|/2))` is unchanged.

4. **Wall absorption for polygon walls is initially `rW[b]` for all side walls.** Do not try to assign different materials to different polygon walls in the initial implementation. That's a future feature.

5. **The +15 dB output trim, 4-channel layout, ER/tail split, silence trim are all post-processing and do not interact with the new geometry code.**

6. **`generateIS2D` must include order 0** (the direct source with no wall reflections). This is `imageSources[0]` with an empty `wallPath`. It represents the direct path `source → receiver` at the horizontal level, combined with the `nz=0` case to give the direct-path Ref.

7. **Lambert scatter (Feature A) must be ported.** It fires on image sources of order 1–3 (horizontal + vertical combined). The scatter logic spawns secondary Refs at slightly offset times. Adapt from `calcRefs` lines ~310–340. Do not omit this — it's important for smooth ER density.

8. **`makeWalls2D` must skip zero-length walls.** When `shapeCornerCut=0` for Octagonal, the corner vertices coincide, producing zero-length walls. These must be detected (`length < 1e-9`) and excluded from the returned wall list.

9. **Visibility test epsilon must be consistent.** Use `EPS = 1e-9` throughout. Tighter epsilons cause false negatives on reflections that just graze a wall corner; looser epsilons cause false positives. The `1e-9` value is appropriate for metre-scale room dimensions.

10. **All four poly shapes route through `synthDirectPath` with the same dispatch.** DIRECT path at `direct_max_order=1` will only produce order-0 (direct path) and order-1 (single-wall) reflections regardless, so the higher order caps for Octagonal/Cathedral don't matter much for DIRECT.

11. **Factory IR regeneration is mandatory after this change.** Any factory IR synthesised with "Cylindrical", "Cathedral", or "Octagonal" will produce acoustically different (better) output post-change. Those IRs must be regenerated with `generate_factory_irs` and the updated sidecar params. Check which factory venues use non-rectangular shapes before cutting the release.

---

## 17. `CLAUDE.md` Additions Required

Add a new section **"Polygon Room Geometry (v2.8.0)"** documenting:

- The dispatch: `shape == "Rectangular"` → `calcRefs`, all others → `calcRefsPolygon`
- `Wall2D` struct and the counter-clockwise winding convention
- `makeWalls2D()` and which shapes it generates
- `generateIS2D()` Borish 2D ISM algorithm outline
- Order limit caps per shape and path-length early termination
- `polygonArea()` / `polygonPerimeter()` and where they're used (RT60, MFP)
- `applyModalBank()` disabled for non-rectangular
- New `IRSynthParams` fields and their ranges
- Shape migration table ("Cylindrical" → "Circular Hall", "L-shaped" → "Rectangular")

Also add to the "Key design decisions" section:

- **Polygon ISM is 2D + vertical (nz), not full 3D** — vertical reflections use the same `nz` loop because all shapes have flat floor/ceiling. Only horizontal reflections use the Borish polygon algorithm.
- **`calcRefs` is untouched** — `"Rectangular"` hard-routes through the original function. IR_11, IR_14 golden values are unaffected.
- **Wall absorption initially uniform** — all polygon side walls use `rW[b]`. Per-wall material is a future feature.
- **Modal bank disabled for non-rectangular** — axial standing-wave modes are only meaningful for rectangular rooms. `applyModalBank()` returns input unchanged for all polygon shapes.
- **Fan/Shoebox supports higher reflection order** — 4 horizontal walls allows order cap of 20 (vs 6 for 12/16-wall shapes). This gives Fan/Shoebox much richer high-order early reflection density than Cathedral or Circular Hall.

---

## 18. Files Changed — Summary Checklist

```
Source/IRSynthEngine.h          — New IRSynthParams fields; Wall2D struct; new fn declarations
Source/IRSynthEngine.cpp        — makeWalls2D; polygonArea/Perimeter; reflect2D;
                                  rayIntersectsSegment; generateIS2D; calcRefsPolygon;
                                  orderLimitForShape; updated calcRT60; updated
                                  renderFDNTail; updated applyModalBank
Source/IRSynthComponent.h       — New slider members
Source/IRSynthComponent.cpp     — Proportion sliders; shape combo changes;
                                  updateShapeControlVisibility(); setParams/getParams
Source/FloorPlanComponent.h     — Updated roomPoly signature
Source/FloorPlanComponent.cpp   — Parametric roomPoly(); updated isInsideRoom();
                                  Circular Hall 16-gon visual; L-shaped removed
Source/PluginProcessor.cpp      — Sidecar read/write; shape migration on load
Tests/PingPolygonTests.cpp      — NEW: IR_27–IR_32, DSP_22
Tools/generate_factory_irs.cpp  — Shape string updates; new param fields for venues
Docs/CLAUDE.md                  — New polygon ISM section + key design decisions
CMakeLists.txt                  — Add PingPolygonTests.cpp to test target
```

---

*End of plan. Implement phases in order. Run `./PingTests` after each phase before proceeding.*
