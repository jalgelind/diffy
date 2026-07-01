# nlohmann/json, vendored as the single-header release (v3.11.3, MIT). Kept as a
# header drop rather than a submodule: the upstream repo is ~100 MB of tests and
# docs for one header. Exposes the canonical `nlohmann_json::nlohmann_json`
# target, so this can be swapped for the real submodule/package later without
# touching any consumer.
add_library(nlohmann_json INTERFACE)
add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
target_include_directories(nlohmann_json INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(nlohmann_json INTERFACE cxx_std_17)
