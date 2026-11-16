cmake_minimum_required(VERSION 3.21)

project(SunlitSummitHeatmap LANGUAGES CXX)

find_package(GDAL CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(lunar_ranker lunar_ranker.cpp)
target_compile_features(lunar_ranker PRIVATE cxx_std_17)
target_link_libraries(lunar_ranker PRIVATE GDAL::GDAL nlohmann_json::nlohmann_json)

add_executable(points_heatmap points_heatmap.cpp)
target_compile_features(points_heatmap PRIVATE cxx_std_17)
target_link_libraries(points_heatmap PRIVATE GDAL::GDAL)

add_executable(raster_preview raster_preview.cpp)
target_compile_features(raster_preview PRIVATE cxx_std_17)
target_link_libraries(raster_preview PRIVATE GDAL::GDAL)
