#pragma once

#include <string>
#include <string_view>

namespace pt {

enum class RenderingApi { Vulkan, D3D12 };

std::string_view RenderingApiKey(RenderingApi api);
std::string_view RenderingApiName(RenderingApi api);
bool ParseRenderingApi(std::string_view text, RenderingApi& out);

bool RenderingApiSelectable();
bool D3D12RunsGame();
void SetD3D12RunsGame(bool available);
bool D3D12Usable(std::string& reason);
void SetD3D12Probe(bool (*probe)(std::string& reason));
void BlockD3D12(std::string reason);
void SkipD3D12Probe();

RenderingApi ChooseRenderingApi(std::string_view cli, std::string_view setting, std::string& note);

enum class AskAnswer { NotShown, Closed, Chosen };
AskAnswer AskRenderingApi(std::string_view title, std::string_view message, RenderingApi& out);

}
