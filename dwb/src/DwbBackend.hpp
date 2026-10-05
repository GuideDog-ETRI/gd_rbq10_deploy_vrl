#pragma once
#include <memory>
#include <string>
#include "PolicyBackend.hpp"

// Legacy DreamWaQ+CENet. Metadata-based policies retain shared PolicyRuntime.
std::unique_ptr<PolicyBackend> makeDwbBackend(const std::string& path, float payloadKg);
