# Build and run the lunar site heatmap

The website posts slider percentages and the maximum slope to a local Python
API. The API runs `lunar_ranker`, which reads the NASA GeoTIFFs and writes a
floating-point suitability raster. `points_heatmap` colorizes that raster from
red (least suitable) to green (most suitable), writes a georeferenced GeoTIFF
to `build/suitability_output.tif`, and creates the PNG displayed by the website.
The output uses the global `ldem_16_uint.tif` grid (5760 × 2880). The south-polar
science-layer scores are averaged into matching global cells; every other valid
global elevation pixel receives a terrain-slope grade. The map is fully opaque
and displays only the red-to-green suitability ramp, with no grayscale
elevation showing through.

## Current raster inputs and scoring

Place the source files anywhere under `Tiff Files`. The program locates them by
filename, including in nested folders:

- Section 1: `ldem_16_uint.tif` elevation data.
- Section 2: `AVGVISIB_85S_060M_201608.tiff` sunlight,
  `AVGVISIB_85S_060M_201608_EARTH.tiff` Earth visibility, and
  `LPSR_85S_060M_201608.tiff` permanently shadowed regions.
- Section 3: `WAC_POLE_ILL_PCT_SOUTH_100M.TIF` illumination observations,
  which have different coverage and resolution from the section 2 grid.
- Section 4: five finalist-site pairs named
  `Site##_final_adj_5mpp_slp.tif` and `Site##_final_adj_5mpp_surf.tif`.
  The slope rasters contain precomputed degrees at 5 m/pixel; the paired
  surface rasters contain elevation in meters. These site windows use lunar
  south-polar stereographic coordinates and do not cover the whole Moon.

The section 2 rasters share a 5058 × 5058 polar grid. The global LDEM files do
not include georeferencing, so the 16-pixels-per-degree grid is assigned a
global lunar longitude/latitude extent before reprojection. Elevation is
converted using the supplied LDEM encoding (`height = value / 2 - 10000`
meters). The maximum-slope setting controls a continuous terrain penalty:
slopes at or above the limit receive the lowest smooth-terrain score, but are
not omitted. One-sided derivatives grade raster edges and cells next to missing
neighbors. Where a science layer is unavailable, the score is normalized over
the criteria available at that pixel; a pixel without any polar science
criterion receives the terrain-only score. These fallback grades keep the map
complete, but are not directly comparable in evidential strength to grades
using the full science-layer set. If the smooth-terrain weight is zero, the
terrain-only fallback is neutral because no weighted scoring criterion is
available outside the science-layer footprint.

Sunlight, Earth-visibility, and LPSR raster scale/offset metadata is applied
before scoring; LPSR values above 0.5 are treated as shadowed. Shadowed pixels
remain in the map and are scored using their other available criteria.
Distance to the nearest shadowed pixel is used as a relative ice-proximity
proxy; this is not a direct water-ice measurement. The WAC illumination byte
values (0–255) are normalized to 0–1 and bilinearly reprojected to the section
2 grid. Where valid WAC data overlaps, the sunlight criterion is the mean of
WAC and section 2 sunlight; elsewhere section 2 sunlight is used alone.
`ldem_4_uint.tif` is a lower-resolution alternative and is not independently
counted as another criterion.

Selecting a finalist site runs a separate score at that site's native 5 m
resolution. The supplied `_slp.tif` values directly drive the smooth-terrain
criterion; sunlight, Earth visibility, shadow proximity, and WAC data are
reprojected to that site's grid. The `_surf.tif` pair provides the elevation
validity mask and geographic context, but elevation is not counted as another
weighted criterion. Invalid edge cells in these site windows are shown with a
neutral grade rather than mistaken for a suitability score.

The temperature-balance slider uses a relative proxy, not measured temperature
or temporal stability. Normalized sunlight raises the proxy; proximity to
permanently shadowed regions lowers it. The temperature score favors the
midpoint between those influences and penalizes warmer or cooler extremes.
There is no measured ice-abundance raster, so LPSR distance is only an ice
proximity proxy. NASA's [LRO Diviner GDR dataset](https://pds.nasa.gov/ds-view/pds/viewDataset.jsp?dsid=LRO-L-DLRE-5-GDR-V1.0)
and [PDS Geosciences Diviner archive](https://pds-geosciences.wustl.edu/missions/lro/diviner.htm)
are the authoritative sources to investigate. The archive contains temperature
observations, not necessarily a ready-made temperature GeoTIFF. For "stable
temperature," use a spatial day/night or seasonal temperature-range product,
then align it to the section 2 grid. A single temperature snapshot is not a
stability score.

## Windows

Install Visual Studio C++ build tools, CMake, and vcpkg. In a VS Code
PowerShell terminal at the project root:

```powershell
$env:VCPKG_ROOT = "D:\dev\vcpkg"
cmake --preset dev
cmake --build --preset dev
python Interface.py
```

Open `http://127.0.0.1:8765/`, adjust the sliders, and select **Run analysis**.
The evaluation layers are combined by the ranker and displayed as one fully
colored red-to-green map aligned with the global LDEM grid; input TIFFs are not
selected or loaded as separate views. Every global pixel is colored. The
status note reports what percentage uses south-polar science layers; the
remaining grades use terrain slope only (or a neutral score when smooth
terrain has zero weight). Use **Map view** to score any of the five finalist
windows at native 5 m/pixel resolution. The map can be panned and zoomed. The
API and website share the same local origin, so no separate web server or CORS
configuration is needed.

While the server is running, edits to HTML/JavaScript automatically refresh
the browser, edits to `Interface.py` restart the API, and edits to C++/CMake
sources trigger a rebuild. A failed automatic C++ build is reported by the API
instead of silently using an outdated executable.

Check that the API is available with:

```powershell
Invoke-WebRequest -UseBasicParsing http://127.0.0.1:8765/api/health
```

The response should be `{"status":"ok"}`. A successful analysis also makes the
colorized GeoTIFF available at
`http://127.0.0.1:8765/build/suitability_output.tif`.

## Linux or Codespaces

Set `VCPKG_ROOT` to the vcpkg installation, then run:

```sh
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset dev
cmake --build --preset dev
python3 Interface.py
```

## Current limitations

- The temperature-balance value is a relative proxy, not true measured
  temperature stability; use day/night or seasonal temperature data for that.
- The shadow-distance score is a proxy for ice proximity, not a measured ice
  abundance layer.
- WAC illumination is blended with section 2 sunlight only where their
  reprojected coverage overlaps.
- Global pixels outside the south-polar science-layer footprint are graded
  from elevation slope only; they are colored for completeness but are less
  informative than science-layer scores.
- The browser displays a PNG preview of the output GeoTIFF; the GeoTIFF itself
  remains available for download.
