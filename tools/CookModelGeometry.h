#pragma once

#include <filesystem>

namespace OpenYAMM::Tools
{
// Geometry passes of the model cook (tools/cook_models.py owns the GLBs and the decisions; this only computes).
// The request and response are little-endian binary files (layout in CookModelGeometry.cpp). Returns 0 on success.
int cookModelGeometry(const std::filesystem::path &request, const std::filesystem::path &response);
}
