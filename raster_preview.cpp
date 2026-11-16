#include <gdal_priv.h>
#include <cpl_string.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
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
  auto* dataset = static_cast<GDALDataset*>(GDALOpen(path, GA_ReadOnly));
  if (!dataset) fail("Cannot open raster: " + std::string(path));
  return Dataset(dataset, GDALClose);
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 4) {
      fail("Usage: raster_preview <input.tif> <layer-kind> <preview.png>");
    }
    GDALAllRegister();
    Dataset source = openRaster(argv[1]);
    const std::string kind = argv[2];
    const int width = source->GetRasterXSize();
    const int height = source->GetRasterYSize();
    auto* band = source->GetRasterBand(1);
    if (!band || width <= 0 || height <= 0) {
      fail("Input raster has invalid dimensions or no data band.");
    }
    std::vector<float> values(static_cast<size_t>(width) * height);
    std::vector<unsigned char> valid(values.size(), 0);
    if (band->RasterIO(GF_Read, 0, 0, width, height, values.data(), width,
                       height, GDT_Float32, 0, 0, nullptr) != CE_None) {
      fail("Could not read raster pixels.");
    }

    int hasNoData = FALSE;
    const double noData = band->GetNoDataValue(&hasNoData);
    int hasScale = FALSE;
    int hasOffset = FALSE;
    double scale = band->GetScale(&hasScale);
    double offset = band->GetOffset(&hasOffset);
    if (!hasScale) scale = 1.0;
    if (!hasOffset) offset = 0.0;
    if (!std::isfinite(scale) || !std::isfinite(offset)) {
      fail("Input raster has invalid scale or offset metadata.");
    }

    float minimum = std::numeric_limits<float>::infinity();
    float maximum = -std::numeric_limits<float>::infinity();
    for (size_t i = 0; i < values.size(); ++i) {
      float& value = values[i];
      if (!std::isfinite(value) ||
          (hasNoData && value == static_cast<float>(noData))) {
        continue;
      }
      value = static_cast<float>(value * scale + offset);
      if (kind == "elevation") value = value * 0.5F - 10000.0F;
      if (!std::isfinite(value)) continue;
      valid[i] = 1;
      minimum = std::min(minimum, value);
      maximum = std::max(maximum, value);
    }
    if (!std::isfinite(minimum) || !std::isfinite(maximum)) {
      fail("Input raster contains no valid pixels.");
    }

    std::vector<unsigned char> red(values.size());
    std::vector<unsigned char> green(values.size());
    std::vector<unsigned char> blue(values.size());
    std::vector<unsigned char> alpha(values.size(), 0);
    const float range = std::max(maximum - minimum, 1.0e-12F);
    for (size_t i = 0; i < values.size(); ++i) {
      const float value = values[i];
      if (!valid[i]) continue;
      alpha[i] = 255;
      if (kind == "shadow") {
        const bool shadowed = value > 0.5F;
        red[i] = shadowed ? 225 : 60;
        green[i] = shadowed ? 92 : 72;
        blue[i] = shadowed ? 50 : 84;
      } else {
        const auto gray = static_cast<unsigned char>(
            std::lround(255.0F * std::clamp((value - minimum) / range, 0.0F,
                                           1.0F)));
        red[i] = green[i] = blue[i] = gray;
      }
    }

    auto* memoryDriver = GetGDALDriverManager()->GetDriverByName("MEM");
    auto* png = GetGDALDriverManager()->GetDriverByName("PNG");
    if (!memoryDriver || !png) {
      fail("GDAL MEM or PNG driver is unavailable.");
    }
    Dataset previewBuffer(
        memoryDriver->Create("", width, height, 4, GDT_Byte, nullptr),
        GDALClose);
    if (!previewBuffer) fail("Could not allocate the preview raster.");
    const std::vector<const std::vector<unsigned char>*> channels = {
        &red, &green, &blue, &alpha};
    const GDALColorInterp interpretations[] = {GCI_RedBand, GCI_GreenBand,
                                                 GCI_BlueBand, GCI_AlphaBand};
    for (int index = 0; index < 4; ++index) {
      auto* outputBand = previewBuffer->GetRasterBand(index + 1);
      outputBand->SetColorInterpretation(interpretations[index]);
      if (outputBand->RasterIO(
              GF_Write, 0, 0, width, height,
              const_cast<unsigned char*>(channels[index]->data()), width,
              height, GDT_Byte, 0, 0, nullptr) != CE_None) {
        fail("Could not write preview PNG pixels.");
      }
    }
    Dataset preview(png->CreateCopy(argv[3], previewBuffer.get(), FALSE,
                                    nullptr, nullptr, nullptr),
                    GDALClose);
    if (!preview) fail("Could not create preview PNG: " + std::string(argv[3]));
    preview->FlushCache();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "raster_preview: " << error.what() << '\n';
    return 1;
  }
}
