#include <gdal_priv.h>
#include <cpl_string.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Dataset = std::unique_ptr<GDALDataset, decltype(&GDALClose)>;

[[noreturn]] void fail(const std::string& message) {
  throw std::runtime_error(message);
}

Dataset openRaster(const char* path) {
  auto* dataset =
      static_cast<GDALDataset*>(GDALOpen(path, GA_ReadOnly));
  if (!dataset) fail("Cannot open score TIFF: " + std::string(path));
  return Dataset(dataset, GDALClose);
}

void setColorInterpretation(GDALDataset& dataset) {
  dataset.GetRasterBand(1)->SetColorInterpretation(GCI_RedBand);
  dataset.GetRasterBand(2)->SetColorInterpretation(GCI_GreenBand);
  dataset.GetRasterBand(3)->SetColorInterpretation(GCI_BlueBand);
  dataset.GetRasterBand(4)->SetColorInterpretation(GCI_AlphaBand);
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 4) {
      fail("Usage: points_heatmap <scores.tif> <colored-output.tif> <preview.png>");
    }
    GDALAllRegister();
    Dataset scores = openRaster(argv[1]);
    const int width = scores->GetRasterXSize();
    const int height = scores->GetRasterYSize();
    auto* scoreBand = scores->GetRasterBand(1);
    if (!scoreBand) fail("Score TIFF has no raster band.");

    int hasNoData = FALSE;
    const double noData = scoreBand->GetNoDataValue(&hasNoData);
    std::vector<float> values(static_cast<size_t>(width) * height);
    if (scoreBand->RasterIO(GF_Read, 0, 0, width, height, values.data(), width,
                           height, GDT_Float32, 0, 0, nullptr) != CE_None) {
      fail("Could not read score pixels.");
    }

    std::vector<unsigned char> red(values.size());
    std::vector<unsigned char> green(values.size());
    std::vector<unsigned char> blue(values.size(), 0);
    std::vector<unsigned char> alpha(values.size(), 255);
    for (size_t i = 0; i < values.size(); ++i) {
      const float value = values[i];
      if (!std::isfinite(value) ||
          (hasNoData && value == static_cast<float>(noData))) {
        fail("Score TIFF contains ungraded pixels; refusing to create a partially transparent map.");
      }
      const float score = std::clamp(value, 0.0F, 1.0F);
      red[i] = static_cast<unsigned char>(std::lround(255.0F * (1.0F - score)));
      green[i] = static_cast<unsigned char>(std::lround(255.0F * score));
      alpha[i] = 255;
    }

    auto* gtiff = GetGDALDriverManager()->GetDriverByName("GTiff");
    auto* png = GetGDALDriverManager()->GetDriverByName("PNG");
    if (!gtiff || !png) fail("GDAL GeoTIFF or PNG driver is unavailable.");
    char** options = nullptr;
    options = CSLSetNameValue(options, "COMPRESS", "DEFLATE");
    options = CSLSetNameValue(options, "PHOTOMETRIC", "RGB");
    options = CSLSetNameValue(options, "ALPHA", "YES");
    Dataset colored(gtiff->Create(argv[2], width, height, 4, GDT_Byte, options),
                    GDALClose);
    CSLDestroy(options);
    if (!colored) fail("Could not create colored GeoTIFF: " + std::string(argv[2]));

    double transform[6];
    if (scores->GetGeoTransform(transform) != CE_None ||
        colored->SetGeoTransform(transform) != CE_None ||
        colored->SetProjection(scores->GetProjectionRef()) != CE_None) {
      fail("Could not copy georeferencing to the colored GeoTIFF.");
    }
    setColorInterpretation(*colored);
    const std::vector<const std::vector<unsigned char>*> channels = {
        &red, &green, &blue, &alpha};
    for (int bandIndex = 0; bandIndex < 4; ++bandIndex) {
      if (colored->GetRasterBand(bandIndex + 1)
              ->RasterIO(GF_Write, 0, 0, width, height,
                         const_cast<unsigned char*>(
                             channels[bandIndex]->data()),
                         width, height, GDT_Byte, 0, 0, nullptr) != CE_None) {
        fail("Could not write a color channel to the GeoTIFF.");
      }
    }
    colored->FlushCache();

    Dataset preview(png->CreateCopy(argv[3], colored.get(), FALSE, nullptr,
                                    nullptr, nullptr),
                    GDALClose);
    if (!preview) fail("Could not create PNG preview: " + std::string(argv[3]));
    preview->FlushCache();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "points_heatmap: " << error.what() << '\n';
    return 1;
  }
}
