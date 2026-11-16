# NASA Space Apps Sunlit Summit

A lunar landing-site suitability viewer. The website sends its slider weights
to a local API, which runs the C++ raster ranker and returns a zoomable
red-to-green heatmap aligned to the global elevation TIFF. Sunlight, Earth
visibility, shadow proximity, elevation/slope, and overlapping WAC illumination
are evaluated together. Temperature balance is a clearly labeled
sunlight/ice-proximity proxy. Every global LDEM pixel is colored; pixels outside
the south-polar science-layer footprint receive a terrain-only grade and are
identified as less informative in the interface. Five finalist sites also have
native 5 m/pixel LOLA slope and surface rasters that can be scored and viewed
individually from the map selector.

See [TESTING.md](TESTING.md) for build instructions, data inputs, scoring
assumptions, and current limitations.
