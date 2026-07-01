#include "review.hpp"

#include <nlohmann/json.hpp>

namespace diffy::review {

std::string
library_version() {
    return "diffy-review 0 (json " + std::to_string(NLOHMANN_JSON_VERSION_MAJOR) + "." +
           std::to_string(NLOHMANN_JSON_VERSION_MINOR) + "." +
           std::to_string(NLOHMANN_JSON_VERSION_PATCH) + ")";
}

}  // namespace diffy::review
