#pragma once

#include "setup_core.h"
#include "engine/platform/self_integrity.h"

namespace pt::setup {

inline void VerifyIntegrity() {
    CheckCancel();
    if (!pt::integrity::IntegrityOk()) throw std::runtime_error("This setup file is damaged or was modified. Please download it again.");
}

}
