#pragma once

#include <string>
#include <string_view>

namespace pt::boot {

void Phase(std::string_view phase, std::string_view note = {});

double SinceProcessStart();

std::string PhaseText(std::string_view phase, double seconds, std::string_view note);

}
