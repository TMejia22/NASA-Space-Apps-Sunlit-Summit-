#include <gdal_priv.h>
#include <gdal_alg.h>
#include <gdalwarper.h>
#include <cpl_conv.h>
#include <cpl_string.h>
#include <nlohmann/json.hpp>
#include <ogr_spatialref.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;
using Dataset = std::unique_ptr<GDALDataset, decltype(&GDALClose)>;

constexpr float kNoData = -9999.0F;
constexpr float kPi = 3.14159265358979323846F;

[[noreturn]] void fail(const std::string& message) {
  throw std::runtime_error(message);
}

Dataset openRaster(const fs::path& path) {
  auto* dataset =
      static_cast<GDALDataset*>(GDALOpen(path.string().c_str(), GA_ReadOnly));
  if (!dataset) {
    fail("Cannot open raster: " + path.string() + ": " + CPLGetLastErrorMsg());
  }
  return Dataset(dataset, GDALClose);
}

fs::path findFile(const fs::path& root, const std::string& filename) {
  if (!fs::exists(root)) {
    fail("TIFF data folder does not exist: " + root.string());
  }
  for (const auto& entry :
       fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied)) {
    const std::string candidate = entry.path().filename().string();
    if (entry.is_regular_file() && candidate.size() == filename.size() &&
        std::equal(candidate.begin(), candidate.end(), filename.begin(),
                   [](unsigned char a, unsigned char b) {
                     return std::tolower(a) == std::tolower(b);
                   })) {
      return entry.path();
    }
  }
  fail("Required TIFF was not found: " + filename);
}

Dataset createMemoryRaster(int width, int height, GDALDataType type,
                           const double* transform, const char* projection,
                           double noData) {
  auto* driver = GetGDALDriverManager()->GetDriverByName("MEM");
  if (!driver) fail("GDAL MEM driver is unavailable.");
  Dataset dataset(driver->Create("", width, height, 1, type, nullptr), GDALClose);
  if (!dataset) fail("Could not allocate an in-memory raster.");
  dataset->SetGeoTransform(const_cast<double*>(transform));
  dataset->SetProjection(projection);
  dataset->GetRasterBand(1)->SetNoDataValue(noData);
  return dataset;
}

std::string moonGeographicWkt() {
  OGRSpatialReference spatialReference;
  spatialReference.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
  if (spatialReference.importFromProj4("+proj=longlat +R=1737400 +no_defs") !=
      OGRERR_NONE) {
    fail("Could not define the lunar geographic coordinate system.");
  }
  char* wkt = nullptr;
  if (spatialReference.exportToWkt(&wkt) != OGRERR_NONE || !wkt) {
    fail("Could not export the lunar geographic coordinate system.");
  }
  std::string result(wkt);
  CPLFree(wkt);
  return result;
}

Dataset elevationWithGeoreference(GDALDataset& source) {
  double transform[6];
  const bool hasTransform = source.GetGeoTransform(transform) == CE_None;
  const char* projection = source.GetProjectionRef();
  const bool hasProjection = projection && projection[0] != '\0';
  if (hasTransform && hasProjection) {
    auto* copyDriver = GetGDALDriverManager()->GetDriverByName("MEM");
    Dataset copy(copyDriver->CreateCopy("", &source, FALSE, nullptr, nullptr,
                                        nullptr),
                 GDALClose);
    if (!copy) fail("Could not create an in-memory copy of the elevation TIFF.");
    return copy;
  }

  const int width = source.GetRasterXSize();
  const int height = source.GetRasterYSize();
  if (width <= 0 || height <= 0) fail("Elevation TIFF has invalid dimensions.");
  const double fallbackTransform[6] = {
      -180.0, 360.0 / width, 0.0, 90.0, 0.0, -180.0 / height};
  const std::string fallbackProjection = moonGeographicWkt();
  auto prepared = createMemoryRaster(width, height, GDT_Float32,
                                     hasTransform ? transform : fallbackTransform,
                                     hasProjection ? projection
                                                   : fallbackProjection.c_str(),
                                     0.0);
  auto* sourceBand = source.GetRasterBand(1);
  auto* targetBand = prepared->GetRasterBand(1);
  targetBand->DeleteNoDataValue();
  std::vector<float> values(static_cast<size_t>(width) * height);
  if (sourceBand->RasterIO(GF_Read, 0, 0, width, height, values.data(), width,
                           height, GDT_Float32, 0, 0, nullptr) != CE_None) {
    fail("Could not read the elevation TIFF.");
  }
  int hasNoData = FALSE;
  const double sourceNoData = sourceBand->GetNoDataValue(&hasNoData);
  if (hasNoData) {
    targetBand->SetNoDataValue(sourceNoData);
  }
  if (targetBand->RasterIO(GF_Write, 0, 0, width, height, values.data(), width,
                           height, GDT_Float32, 0, 0, nullptr) != CE_None) {
    fail("Could not prepare elevation values for reprojection.");
  }
  return prepared;
}

std::vector<float> warpToReference(GDALDataset& source,
                                   GDALDataset& reference,
                                   GDALResampleAlg resampling,
                                   double destinationNoData = kNoData) {
  const char* projection = reference.GetProjectionRef();
  if (!projection || projection[0] == '\0') {
    fail("The section-2 sunlight raster has no projection metadata.");
  }
  double transform[6];
  if (reference.GetGeoTransform(transform) != CE_None) {
    fail("The section-2 sunlight raster has no geotransform.");
  }
  const int width = reference.GetRasterXSize();
  const int height = reference.GetRasterYSize();
  auto destination =
      createMemoryRaster(width, height, GDT_Float32, transform, projection,
                         destinationNoData);
  destination->GetRasterBand(1)->Fill(destinationNoData);
  const char* sourceProjection = source.GetProjectionRef();
  if (!sourceProjection || sourceProjection[0] == '\0') {
    fail("An input raster is missing projection metadata.");
  }
  if (GDALReprojectImage(&source, sourceProjection, destination.get(),
                         projection, resampling, 0.0, 0.0, nullptr, nullptr,
                         nullptr) != CE_None) {
    fail("Could not reproject an input TIFF: " +
         std::string(CPLGetLastErrorMsg()));
  }
  std::vector<float> values(static_cast<size_t>(width) * height);
  if (destination->GetRasterBand(1)->RasterIO(
          GF_Read, 0, 0, width, height, values.data(), width, height,
          GDT_Float32, 0, 0, nullptr) != CE_None) {
    fail("Could not read a reprojected input raster.");
  }
  return values;
}

void applyBandScaleAndOffset(std::vector<float>& values,
                             GDALRasterBand& band) {
  int hasScale = FALSE;
  int hasOffset = FALSE;
  double scale = band.GetScale(&hasScale);
  double offset = band.GetOffset(&hasOffset);
  if (!hasScale) scale = 1.0;
  if (!hasOffset) offset = 0.0;
  if (!std::isfinite(scale) || !std::isfinite(offset)) {
    fail("A raster has invalid scale or offset metadata.");
  }
  for (float& value : values) {
    if (std::isfinite(value) && value != kNoData) {
      value = static_cast<float>(value * scale + offset);
    }
  }
}

float normalizedPercent(float value, const std::string& layer) {
  if (!std::isfinite(value) || value < 0.0F) {
    fail("The " + layer + " raster contains an invalid score value.");
  }
  if (value <= 1.0001F) return std::clamp(value, 0.0F, 1.0F);
  if (value <= 100.0F) return value / 100.0F;
  fail("The " + layer + " raster must use values from 0–1 or 0–100.");
}

float calculateSlope(const std::vector<float>& elevation, int width, int height,
                     int x, int y, double pixelSizeX, double pixelSizeY) {
  const auto valid = [&](int px, int py) {
    if (px < 0 || py < 0 || px >= width || py >= height) return false;
    const float value = elevation[static_cast<size_t>(py) * width + px];
    return std::isfinite(value) && value != kNoData;
  };
  if (!valid(x, y)) {
    return std::numeric_limits<float>::quiet_NaN();
  }
  const float center = elevation[static_cast<size_t>(y) * width + x];
  const bool hasLeft = valid(x - 1, y);
  const bool hasRight = valid(x + 1, y);
  const bool hasUp = valid(x, y - 1);
  const bool hasDown = valid(x, y + 1);
  const float dzdx = hasLeft && hasRight
                         ? (elevation[static_cast<size_t>(y) * width + x + 1] -
                            elevation[static_cast<size_t>(y) * width + x - 1]) /
                               static_cast<float>(2.0 * pixelSizeX)
                         : hasRight
                               ? (elevation[static_cast<size_t>(y) * width +
                                            x + 1] -
                                  center) /
                                     static_cast<float>(pixelSizeX)
                               : hasLeft
                                     ? (center -
                                        elevation[static_cast<size_t>(y) *
                                                      width +
                                                  x - 1]) /
                                           static_cast<float>(pixelSizeX)
                                     : 0.0F;
  const float dzdy = hasUp && hasDown
                         ? (elevation[static_cast<size_t>(y + 1) * width + x] -
                            elevation[static_cast<size_t>(y - 1) * width + x]) /
                               static_cast<float>(2.0 * pixelSizeY)
                         : hasDown
                               ? (elevation[static_cast<size_t>(y + 1) * width +
                                            x] -
                                  center) /
                                     static_cast<float>(pixelSizeY)
                               : hasUp
                                     ? (center -
                                        elevation[static_cast<size_t>(y - 1) *
                                                      width +
                                                  x]) /
                                           static_cast<float>(pixelSizeY)
                                     : 0.0F;
  return std::atan(std::hypot(dzdx, dzdy)) * 180.0F / kPi;
}

void writeScores(const fs::path& path, GDALDataset& reference,
                 const std::vector<float>& scores) {
  auto* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
  if (!driver) fail("GDAL GeoTIFF driver is unavailable.");
  char** options = nullptr;
  options = CSLSetNameValue(options, "COMPRESS", "DEFLATE");
  Dataset output(driver->Create(path.string().c_str(),
                                reference.GetRasterXSize(),
                                reference.GetRasterYSize(), 1, GDT_Float32,
                                options),
                 GDALClose);
  CSLDestroy(options);
  if (!output) fail("Could not create the scored GeoTIFF: " + path.string());
  double transform[6];
  if (reference.GetGeoTransform(transform) != CE_None ||
      output->SetGeoTransform(transform) != CE_None ||
      output->SetProjection(reference.GetProjectionRef()) != CE_None) {
    fail("Could not copy georeferencing to the scored GeoTIFF.");
  }
  auto* band = output->GetRasterBand(1);
  band->SetNoDataValue(kNoData);
  if (band->RasterIO(GF_Write, 0, 0, output->GetRasterXSize(),
                     output->GetRasterYSize(),
                     const_cast<float*>(scores.data()),
                     output->GetRasterXSize(), output->GetRasterYSize(),
                     GDT_Float32, 0, 0, nullptr) != CE_None) {
    fail("Could not write scored pixels.");
  }
}

std::vector<float> resampleScoresToGlobalGrid(
    const std::vector<float>& scores, GDALDataset& analysisGrid,
    GDALDataset& globalGrid) {
  double transform[6];
  if (analysisGrid.GetGeoTransform(transform) != CE_None) {
    fail("The analysis raster has no geotransform.");
  }
  auto source = createMemoryRaster(
      analysisGrid.GetRasterXSize(), analysisGrid.GetRasterYSize(),
      GDT_Float32, transform, analysisGrid.GetProjectionRef(), kNoData);
  if (source->GetRasterBand(1)->RasterIO(
          GF_Write, 0, 0, analysisGrid.GetRasterXSize(),
          analysisGrid.GetRasterYSize(),
          const_cast<float*>(scores.data()), analysisGrid.GetRasterXSize(),
          analysisGrid.GetRasterYSize(), GDT_Float32, 0, 0, nullptr) !=
      CE_None) {
    fail("Could not prepare scores for the global map grid.");
  }
  return warpToReference(*source, globalGrid, GRA_Average);
}

std::vector<float> scoreGlobalTerrain(GDALDataset& globalGrid,
                                      double maxSlope, double smoothWeight) {
  const int width = globalGrid.GetRasterXSize();
  const int height = globalGrid.GetRasterYSize();
  const size_t count = static_cast<size_t>(width) * height;
  auto* band = globalGrid.GetRasterBand(1);
  std::vector<float> elevation(count, kNoData);
  if (band->RasterIO(GF_Read, 0, 0, width, height, elevation.data(), width,
                     height, GDT_Float32, 0, 0, nullptr) != CE_None) {
    fail("Could not read the global elevation pixels.");
  }
  int hasNoData = FALSE;
  const double noData = band->GetNoDataValue(&hasNoData);
  std::vector<unsigned char> valid(count, 1);
  for (size_t i = 0; i < count; ++i) {
    if (!std::isfinite(elevation[i]) ||
        (hasNoData && elevation[i] == static_cast<float>(noData))) {
      valid[i] = 0;
      elevation[i] = kNoData;
    }
  }
  applyBandScaleAndOffset(elevation, *band);
  for (size_t i = 0; i < count; ++i) {
    if (valid[i]) elevation[i] = elevation[i] * 0.5F - 10000.0F;
  }

  double transform[6];
  if (globalGrid.GetGeoTransform(transform) != CE_None) {
    fail("The global elevation raster has no geotransform.");
  }
  const double longitudeStep = std::hypot(transform[1], transform[2]);
  const double latitudeStep = std::hypot(transform[4], transform[5]);
  if (longitudeStep <= 0.0 || latitudeStep <= 0.0) {
    fail("The global elevation raster has invalid pixel dimensions.");
  }

  const double metersPerDegree = 1737400.0 * kPi / 180.0;
  std::vector<float> scores(count, kNoData);
  for (int y = 0; y < height; ++y) {
    const double latitude =
        transform[3] + (static_cast<double>(y) + 0.5) * transform[5];
    const double pixelSizeX = std::max(
        metersPerDegree * longitudeStep *
            std::abs(std::cos(latitude * kPi / 180.0)),
        0.001);
    const double pixelSizeY = metersPerDegree * latitudeStep;
    for (int x = 0; x < width; ++x) {
      const size_t i = static_cast<size_t>(y) * width + x;
      if (!valid[i]) continue;
      const float slope = calculateSlope(elevation, width, height, x, y,
                                         pixelSizeX, pixelSizeY);
      if (!std::isfinite(slope)) {
        fail("Could not derive a terrain grade for a valid elevation pixel.");
      }
      scores[i] = smoothWeight > 0.0
                      ? 1.0F - std::clamp(
                                  slope / static_cast<float>(maxSlope), 0.0F,
                                  1.0F)
                      : 0.5F;
    }
  }
  return scores;
}

json scoreFinalistSite(const fs::path& dataRoot, const std::string& siteId,
                       const fs::path& outputPath, const json& weights,
                       double maxSlope) {
  const std::vector<std::string> siteIds = {
      "Site01", "Site04", "Site07", "Site11", "Site23"};
  if (std::find(siteIds.begin(), siteIds.end(), siteId) == siteIds.end()) {
    fail("Unknown finalist site: " + siteId);
  }
  const std::string prefix = siteId + "_final_adj_5mpp_";
  Dataset siteSlope = openRaster(
      findFile(dataRoot, prefix + "slp.tif"));
  Dataset siteSurface = openRaster(
      findFile(dataRoot, prefix + "surf.tif"));
  Dataset sunlight =
      openRaster(findFile(dataRoot, "AVGVISIB_85S_060M_201608.tiff"));
  Dataset earth =
      openRaster(findFile(dataRoot, "AVGVISIB_85S_060M_201608_EARTH.tiff"));
  Dataset lpsr = openRaster(findFile(dataRoot, "LPSR_85S_060M_201608.tiff"));
  Dataset wac =
      openRaster(findFile(dataRoot, "WAC_POLE_ILL_PCT_SOUTH_100M.TIF"));

  const int width = siteSlope->GetRasterXSize();
  const int height = siteSlope->GetRasterYSize();
  const size_t count = static_cast<size_t>(width) * height;
  if (width <= 0 || height <= 0 ||
      siteSurface->GetRasterXSize() != width ||
      siteSurface->GetRasterYSize() != height) {
    fail("The finalist site's slope and surface rasters do not match.");
  }
  double transform[6];
  const char* projection = siteSlope->GetProjectionRef();
  if (siteSlope->GetGeoTransform(transform) != CE_None ||
      !projection || projection[0] == '\0') {
    fail("The finalist site's slope raster lacks georeferencing.");
  }
  double surfaceTransform[6];
  if (siteSurface->GetGeoTransform(surfaceTransform) != CE_None ||
      !std::equal(std::begin(transform), std::end(transform),
                  std::begin(surfaceTransform))) {
    fail("The finalist site's slope and surface grids do not align.");
  }
  const char* surfaceProjection = siteSurface->GetProjectionRef();
  OGRSpatialReference slopeReference;
  OGRSpatialReference surfaceReference;
  if (!surfaceProjection ||
      slopeReference.SetFromUserInput(projection) != OGRERR_NONE ||
      surfaceReference.SetFromUserInput(surfaceProjection) != OGRERR_NONE ||
      !slopeReference.IsSame(&surfaceReference)) {
    fail("The finalist site's slope and surface coordinate systems do not match.");
  }

  auto readBand = [&](GDALRasterBand& band, const std::string& label) {
    std::vector<float> values(count, kNoData);
    if (band.RasterIO(GF_Read, 0, 0, width, height, values.data(), width,
                      height, GDT_Float32, 0, 0, nullptr) != CE_None) {
      fail("Could not read the finalist site's " + label + " raster.");
    }
    return values;
  };
  std::vector<float> slopeValues = readBand(*siteSlope->GetRasterBand(1), "slope");
  std::vector<float> surfaceValues =
      readBand(*siteSurface->GetRasterBand(1), "surface");
  auto isValid = [](float value, GDALRasterBand& band) {
    int hasNoData = FALSE;
    const double noData = band.GetNoDataValue(&hasNoData);
    return std::isfinite(value) &&
           (!hasNoData || value != static_cast<float>(noData));
  };
  auto* slopeBand = siteSlope->GetRasterBand(1);
  auto* surfaceBand = siteSurface->GetRasterBand(1);
  std::vector<unsigned char> validSitePixels(count, 0);
  size_t validSiteCount = 0;
  for (size_t i = 0; i < count; ++i) {
    validSitePixels[i] =
        isValid(slopeValues[i], *slopeBand) &&
        isValid(surfaceValues[i], *surfaceBand);
    if (validSitePixels[i]) ++validSiteCount;
  }
  if (validSiteCount == 0) {
    fail("The finalist site's slope and surface rasters contain no valid pixels.");
  }

  std::vector<float> sunValues =
      warpToReference(*sunlight, *siteSlope, GRA_Bilinear);
  std::vector<float> earthValues =
      warpToReference(*earth, *siteSlope, GRA_Bilinear);
  std::vector<float> wacValues =
      warpToReference(*wac, *siteSlope, GRA_Bilinear);
  applyBandScaleAndOffset(sunValues, *sunlight->GetRasterBand(1));
  applyBandScaleAndOffset(earthValues, *earth->GetRasterBand(1));
  applyBandScaleAndOffset(wacValues, *wac->GetRasterBand(1));
  for (float& value : wacValues) {
    if (std::isfinite(value) && value != kNoData) value /= 255.0F;
  }
  const size_t wacOverlapPixels = static_cast<size_t>(std::count_if(
      wacValues.begin(), wacValues.end(),
      [](float value) { return std::isfinite(value) && value != kNoData; }));

  const double sunlightWeight = weights.at("sunlight").get<double>();
  const double earthWeight = weights.at("earth").get<double>();
  const double iceWeight = weights.at("ice").get<double>();
  const double temperatureWeight = weights.at("temp").get<double>();
  const double smoothWeight = weights.at("smooth").get<double>();
  std::vector<float> shadowValues =
      warpToReference(*lpsr, *sunlight, GRA_NearestNeighbour);
  applyBandScaleAndOffset(shadowValues, *lpsr->GetRasterBand(1));
  double polarTransform[6];
  if (sunlight->GetGeoTransform(polarTransform) != CE_None) {
    fail("Sunlight raster has no geotransform.");
  }
  const int polarWidth = sunlight->GetRasterXSize();
  const int polarHeight = sunlight->GetRasterYSize();
  const size_t polarCount = static_cast<size_t>(polarWidth) * polarHeight;
  std::vector<unsigned char> shadowValid(polarCount, 0);
  std::vector<unsigned char> maskValues(polarCount, 255);
  bool hasShadowCells = false;
  for (size_t i = 0; i < polarCount; ++i) {
    if (std::isfinite(shadowValues[i]) && shadowValues[i] != kNoData) {
      shadowValid[i] = 1;
      maskValues[i] = shadowValues[i] > 0.5F ? 1 : 0;
      hasShadowCells = hasShadowCells || maskValues[i] == 1;
    }
  }
  std::vector<float> iceDistance(count, kNoData);
  std::vector<float> siteShadowValid(count, kNoData);
  float maxIceDistance = 0.0F;
  if (hasShadowCells) {
    Dataset mask = createMemoryRaster(
        polarWidth, polarHeight, GDT_Byte, polarTransform,
        sunlight->GetProjectionRef(), 255.0);
    auto* maskBand = mask->GetRasterBand(1);
    if (maskBand->RasterIO(GF_Write, 0, 0, polarWidth, polarHeight,
                           maskValues.data(), polarWidth, polarHeight,
                           GDT_Byte, 0, 0, nullptr) != CE_None) {
      fail("Could not prepare the finalist shadow mask.");
    }
    Dataset distance = createMemoryRaster(
        polarWidth, polarHeight, GDT_Float32, polarTransform,
        sunlight->GetProjectionRef(), kNoData);
    distance->GetRasterBand(1)->Fill(kNoData);
    char** proximityOptions = nullptr;
    proximityOptions = CSLSetNameValue(proximityOptions, "VALUES", "1");
    proximityOptions = CSLSetNameValue(proximityOptions, "DISTUNITS", "GEO");
    const CPLErr proximityResult =
        GDALComputeProximity(maskBand, distance->GetRasterBand(1),
                             proximityOptions, nullptr, nullptr);
    CSLDestroy(proximityOptions);
    if (proximityResult != CE_None) {
      fail("Could not calculate distance to shadowed regions for the finalist site.");
    }
    std::vector<float> polarDistances(polarCount, kNoData);
    if (distance->GetRasterBand(1)->RasterIO(
            GF_Read, 0, 0, polarWidth, polarHeight, polarDistances.data(),
            polarWidth, polarHeight, GDT_Float32, 0, 0, nullptr) != CE_None) {
      fail("Could not read finalist shadow distances.");
    }
    for (size_t i = 0; i < polarCount; ++i) {
      if (shadowValid[i] && std::isfinite(polarDistances[i]) &&
          polarDistances[i] != kNoData) {
        maxIceDistance = std::max(maxIceDistance, polarDistances[i]);
      }
    }
    iceDistance = warpToReference(*distance, *siteSlope, GRA_Bilinear);
    siteShadowValid = warpToReference(*mask, *siteSlope, GRA_NearestNeighbour);
  }

  std::vector<float> scores(count, 0.5F);
  size_t scienceCoveragePixels = 0;
  double bestScore = -1.0;
  int bestX = -1;
  int bestY = -1;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const size_t i = static_cast<size_t>(y) * width + x;
      if (!validSitePixels[i]) continue;
      const float slope = slopeValues[i];
      if (slope < 0.0F || slope > 90.0F) {
        fail("The finalist slope raster contains values outside 0-90 degrees.");
      }
      const bool hasSunlight =
          std::isfinite(sunValues[i]) && sunValues[i] != kNoData;
      const bool hasEarth =
          std::isfinite(earthValues[i]) && earthValues[i] != kNoData;
      const bool hasIce =
          std::isfinite(siteShadowValid[i]) && siteShadowValid[i] != kNoData &&
          std::isfinite(iceDistance[i]) && iceDistance[i] != kNoData;
      if (hasSunlight || hasEarth || hasIce) ++scienceCoveragePixels;
      float sunlightScore =
          hasSunlight ? normalizedPercent(sunValues[i], "sunlight") : 0.5F;
      if (hasSunlight && std::isfinite(wacValues[i]) &&
          wacValues[i] != kNoData) {
        sunlightScore =
            0.5F * (sunlightScore + std::clamp(wacValues[i], 0.0F, 1.0F));
      }
      const float earthScore =
          hasEarth ? normalizedPercent(earthValues[i], "Earth visibility")
                   : 0.5F;
      const float iceScore =
          hasIce && maxIceDistance > 0.0F
              ? 1.0F - std::clamp(iceDistance[i] / maxIceDistance, 0.0F, 1.0F)
              : hasIce ? 1.0F : 0.5F;
      const float temperatureScore =
          1.0F - std::abs(
                     0.5F * ((hasSunlight ? sunlightScore : 0.5F) +
                             (hasIce ? 1.0F - iceScore : 0.5F)) -
                     0.5F) *
                     2.0F;
      const float smoothScore =
          1.0F - std::clamp(slope / static_cast<float>(maxSlope), 0.0F, 1.0F);
      double weightedScore = 0.0;
      double availableWeight = 0.0;
      const auto addCriterion = [&](double weight, float value) {
        if (weight > 0.0) {
          weightedScore += weight * value;
          availableWeight += weight;
        }
      };
      if (hasSunlight) addCriterion(sunlightWeight, sunlightScore);
      if (hasEarth) addCriterion(earthWeight, earthScore);
      if (hasIce) addCriterion(iceWeight, iceScore);
      if (hasSunlight || hasIce) {
        addCriterion(temperatureWeight, temperatureScore);
      }
      addCriterion(smoothWeight, smoothScore);
      const double score = availableWeight > 0.0
                               ? weightedScore / availableWeight
                               : 0.5;
      scores[i] = static_cast<float>(score);
      if (score > bestScore) {
        bestScore = score;
        bestX = x;
        bestY = y;
      }
    }
  }
  if (bestX < 0) fail("Could not select a valid pixel in the finalist site.");
  writeScores(outputPath, *siteSlope, scores);
  return {
      {"site", siteId},
      {"width", width},
      {"height", height},
      {"best", {{"x", bestX}, {"y", bestY}}},
      {"bestScore", bestScore},
      {"validPixels", validSiteCount},
      {"totalPixels", count},
      {"scienceCoveragePixels", scienceCoveragePixels},
      {"wacOverlapPixels", wacOverlapPixels},
      {"pixelSizeMeters", 5},
      {"surfaceIsScoringCriterion", false},
  };
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 3 && argc != 5) {
      fail("Usage: lunar_ranker <tiff-folder> <score-output.tif> [--site <SiteXX>]");
    }
    if (argc == 5 && std::string(argv[3]) != "--site") {
      fail("The optional finalist mode must use --site <SiteXX>.");
    }
    GDALAllRegister();
    const fs::path dataRoot = argv[1];
    const fs::path outputPath = argv[2];
    const std::string siteId = argc == 5 ? argv[4] : "";
    const json request = json::parse(std::cin);
    const json& weights = request.at("weights");
    const double maxSlope = request.at("limits").at("maxSlope").get<double>();
    if (!std::isfinite(maxSlope) || maxSlope < 2.0 || maxSlope > 20.0) {
      fail("Maximum slope must be between 2 and 20 degrees.");
    }
    for (const char* criterion : {"sunlight", "earth", "ice", "temp", "smooth"}) {
      const double weight = weights.at(criterion).get<double>();
      if (!std::isfinite(weight) || weight < 0.0 || weight > 100.0) {
        fail(std::string("Weight '") + criterion +
             "' must be between 0 and 100.");
      }
    }
    const double sunlightWeight = weights.at("sunlight").get<double>();
    const double earthWeight = weights.at("earth").get<double>();
    const double iceWeight = weights.at("ice").get<double>();
    const double temperatureWeight = weights.at("temp").get<double>();
    const double smoothWeight = weights.at("smooth").get<double>();
    const double weightTotal =
        sunlightWeight + earthWeight + iceWeight + temperatureWeight +
        smoothWeight;
    if (!std::isfinite(weightTotal) || weightTotal <= 0.0) {
      fail("At least one available criterion must have a positive weight.");
    }

    if (!siteId.empty()) {
      std::cout << scoreFinalistSite(dataRoot, siteId, outputPath, weights,
                                     maxSlope)
                       .dump()
                << '\n';
      return 0;
    }

    Dataset sunlight =
        openRaster(findFile(dataRoot, "AVGVISIB_85S_060M_201608.tiff"));
    Dataset earth =
        openRaster(findFile(dataRoot, "AVGVISIB_85S_060M_201608_EARTH.tiff"));
    Dataset lpsr = openRaster(findFile(dataRoot, "LPSR_85S_060M_201608.tiff"));
    Dataset wac =
        openRaster(findFile(dataRoot, "WAC_POLE_ILL_PCT_SOUTH_100M.TIF"));
    Dataset dem = openRaster(findFile(dataRoot, "ldem_16_uint.tif"));
    Dataset preparedDem = elevationWithGeoreference(*dem);

    const int width = sunlight->GetRasterXSize();
    const int height = sunlight->GetRasterYSize();
    const size_t count = static_cast<size_t>(width) * height;
    std::vector<float> sunValues =
        warpToReference(*sunlight, *sunlight, GRA_Bilinear);
    std::vector<float> earthValues =
        warpToReference(*earth, *sunlight, GRA_Bilinear);
    std::vector<float> shadowValues =
        warpToReference(*lpsr, *sunlight, GRA_NearestNeighbour);
    std::vector<float> wacValues =
        warpToReference(*wac, *sunlight, GRA_Bilinear);
    applyBandScaleAndOffset(sunValues, *sunlight->GetRasterBand(1));
    applyBandScaleAndOffset(earthValues, *earth->GetRasterBand(1));
    applyBandScaleAndOffset(shadowValues, *lpsr->GetRasterBand(1));
    applyBandScaleAndOffset(wacValues, *wac->GetRasterBand(1));
    for (float& value : wacValues) {
      if (std::isfinite(value) && value != kNoData) value /= 255.0F;
    }
    const size_t wacOverlapPixels = static_cast<size_t>(std::count_if(
        wacValues.begin(), wacValues.end(),
        [](float value) { return std::isfinite(value) && value != kNoData; }));
    if (wacOverlapPixels == 0) {
      fail("WAC illumination does not overlap the section-2 scoring grid.");
    }
    std::vector<float> demRaw =
        warpToReference(*preparedDem, *sunlight, GRA_Bilinear);
    for (float& value : demRaw) {
      if (std::isfinite(value) && value != kNoData) {
        value = value * 0.5F - 10000.0F;
      }
    }

    double transform[6];
    if (sunlight->GetGeoTransform(transform) != CE_None) {
      fail("Sunlight raster has no geotransform.");
    }
    const double pixelSizeX = std::hypot(transform[1], transform[4]);
    const double pixelSizeY = std::hypot(transform[2], transform[5]);
    if (pixelSizeX <= 0.0 || pixelSizeY <= 0.0) {
      fail("Sunlight raster has invalid pixel dimensions.");
    }

    std::vector<unsigned char> shadowMask(count, 0);
    std::vector<unsigned char> shadowValid(count, 0);
    Dataset mask = createMemoryRaster(width, height, GDT_Byte, transform,
                                      sunlight->GetProjectionRef(), 255.0);
    auto* maskBand = mask->GetRasterBand(1);
    std::vector<unsigned char> maskValues(count, 255);
    bool hasShadowCells = false;
    for (size_t i = 0; i < count; ++i) {
      if (std::isfinite(shadowValues[i]) && shadowValues[i] != kNoData) {
        shadowValid[i] = 1;
        shadowMask[i] = shadowValues[i] > 0.5F ? 1 : 0;
        maskValues[i] = shadowMask[i];
        hasShadowCells = hasShadowCells || shadowMask[i] == 1;
      }
    }
    if (maskBand->RasterIO(GF_Write, 0, 0, width, height, maskValues.data(),
                           width, height, GDT_Byte, 0, 0, nullptr) != CE_None) {
      fail("Could not prepare the shadow mask.");
    }

    std::vector<float> iceDistance(count, kNoData);
    float maxIceDistance = 0.0F;
    if (hasShadowCells) {
      Dataset distance = createMemoryRaster(
          width, height, GDT_Float32, transform, sunlight->GetProjectionRef(),
          kNoData);
      distance->GetRasterBand(1)->Fill(kNoData);
      char** proximityOptions = nullptr;
      proximityOptions = CSLSetNameValue(proximityOptions, "VALUES", "1");
      proximityOptions = CSLSetNameValue(proximityOptions, "DISTUNITS", "GEO");
      const CPLErr proximityResult =
          GDALComputeProximity(maskBand, distance->GetRasterBand(1),
                               proximityOptions, nullptr, nullptr);
      CSLDestroy(proximityOptions);
      if (proximityResult != CE_None) {
        fail("Could not calculate distance to permanently shadowed regions.");
      }
      if (distance->GetRasterBand(1)->RasterIO(
              GF_Read, 0, 0, width, height, iceDistance.data(), width, height,
              GDT_Float32, 0, 0, nullptr) != CE_None) {
        fail("Could not read distance-to-shadow values.");
      }
      for (size_t i = 0; i < count; ++i) {
        if (shadowValid[i] && std::isfinite(iceDistance[i]) &&
            iceDistance[i] != kNoData) {
          maxIceDistance = std::max(maxIceDistance, iceDistance[i]);
        }
      }
    }

    std::vector<float> scores(count, kNoData);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const size_t i = static_cast<size_t>(y) * width + x;
        if (!std::isfinite(demRaw[i]) || demRaw[i] == kNoData) continue;
        const float slope = calculateSlope(demRaw, width, height, x, y,
                                           pixelSizeX, pixelSizeY);
        if (!std::isfinite(slope)) continue;

        const bool hasSunlight =
            std::isfinite(sunValues[i]) && sunValues[i] != kNoData;
        const bool hasEarth =
            std::isfinite(earthValues[i]) && earthValues[i] != kNoData;
        const bool hasIce =
            shadowValid[i] && std::isfinite(iceDistance[i]) &&
            iceDistance[i] != kNoData;
        if (!hasSunlight && !hasEarth && !hasIce) continue;
        float sunlightScore = 0.5F;
        if (hasSunlight) {
          sunlightScore = normalizedPercent(sunValues[i], "sunlight");
        }
        if (hasSunlight && std::isfinite(wacValues[i]) &&
            wacValues[i] != kNoData) {
          const float wacScore =
              std::clamp(wacValues[i], 0.0F, 1.0F);
          sunlightScore = 0.5F * (sunlightScore + wacScore);
        }
        const float earthScore =
            hasEarth ? normalizedPercent(earthValues[i], "Earth visibility")
                     : 0.5F;
        const float iceScore = hasIce
                                   ? (maxIceDistance > 0.0F
                                          ? 1.0F - std::clamp(
                                                       iceDistance[i] /
                                                           maxIceDistance,
                                                       0.0F, 1.0F)
                                          : 1.0F)
                                   : 0.5F;
        const float estimatedTemperature = 0.5F *
            ((hasSunlight ? sunlightScore : 0.5F) +
             (hasIce ? 1.0F - iceScore : 0.5F));
        const float temperatureScore =
            1.0F - std::abs(estimatedTemperature - 0.5F) * 2.0F;
        const float smoothScore =
            1.0F - std::clamp(slope / static_cast<float>(maxSlope), 0.0F, 1.0F);
        double weightedScore = 0.0;
        double availableWeight = 0.0;
        const auto addCriterion = [&](double weight, float score) {
          if (weight > 0.0) {
            weightedScore += weight * score;
            availableWeight += weight;
          }
        };
        if (hasSunlight) addCriterion(sunlightWeight, sunlightScore);
        if (hasEarth) addCriterion(earthWeight, earthScore);
        if (hasIce) addCriterion(iceWeight, iceScore);
        if (hasSunlight || hasIce) {
          addCriterion(temperatureWeight, temperatureScore);
        }
        addCriterion(smoothWeight, smoothScore);
        scores[i] = availableWeight > 0.0
                        ? static_cast<float>(weightedScore / availableWeight)
                        : 0.5F;
      }
    }

    std::vector<float> polarScores =
        resampleScoresToGlobalGrid(scores, *sunlight, *preparedDem);
    std::vector<float>().swap(sunValues);
    std::vector<float>().swap(earthValues);
    std::vector<float>().swap(shadowValues);
    std::vector<float>().swap(wacValues);
    std::vector<float>().swap(demRaw);
    std::vector<float>().swap(iceDistance);
    std::vector<float>().swap(scores);
    std::vector<unsigned char>().swap(shadowMask);
    std::vector<unsigned char>().swap(shadowValid);
    std::vector<unsigned char>().swap(maskValues);
    mask.reset();
    std::vector<float> globalScores =
        scoreGlobalTerrain(*preparedDem, maxSlope, smoothWeight);
    const int globalWidth = preparedDem->GetRasterXSize();
    const int globalHeight = preparedDem->GetRasterYSize();
    const size_t globalCount =
        static_cast<size_t>(globalWidth) * globalHeight;
    size_t polarScoredPixels = 0;
    for (size_t i = 0; i < globalCount; ++i) {
      if (std::isfinite(polarScores[i]) && polarScores[i] != kNoData) {
        globalScores[i] = polarScores[i];
        ++polarScoredPixels;
      }
    }

    double globalBestScore = -1.0;
    int globalBestX = -1;
    int globalBestY = -1;
    for (int y = 0; y < globalHeight; ++y) {
      for (int x = 0; x < globalWidth; ++x) {
        const size_t i = static_cast<size_t>(y) * globalWidth + x;
        const float score = polarScoredPixels > 0 ? polarScores[i]
                                                  : globalScores[i];
        if (std::isfinite(score) && score != kNoData &&
            score > globalBestScore) {
          globalBestScore = score;
          globalBestX = x;
          globalBestY = y;
        }
      }
    }
    if (globalBestX < 0) {
      fail("No valid global elevation pixels are available for grading.");
    }
    const size_t gradedPixels = static_cast<size_t>(std::count_if(
        globalScores.begin(), globalScores.end(),
        [](float score) { return std::isfinite(score) && score != kNoData; }));
    if (gradedPixels != globalCount) {
      fail("Some global elevation pixels cannot be graded.");
    }
    writeScores(outputPath, *preparedDem, globalScores);
    const json result = {
        {"width", globalWidth},
        {"height", globalHeight},
        {"best", {{"x", globalBestX}, {"y", globalBestY}}},
        {"analysisWidth", width},
        {"analysisHeight", height},
        {"activeCriteria",
         {"sunlight", "earth", "ice", "temperature-proxy", "smooth"}},
        {"temperatureIncluded", true},
        {"temperatureIsProxy", true},
        {"wacOverlapPixels", wacOverlapPixels},
        {"gradedPixels", gradedPixels},
        {"totalPixels", globalCount},
        {"polarScoredPixels", polarScoredPixels},
        {"fallbackPixels", globalCount - polarScoredPixels},
        {"scoreRange", {{"minimum", 0.0}, {"maximum", 1.0}}},
    };
    std::cout << result.dump() << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "lunar_ranker: " << error.what() << '\n';
    return 1;
  }
}
