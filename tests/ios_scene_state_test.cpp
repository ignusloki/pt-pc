#include "engine/platform/ios_scene_state.h"
#include <cassert>

int main() {
    PTIosSceneState state{};
    int phone = 0, airplay = 0, another_window = 0;
    // An external display can be connected before the application scene exists.
    assert(!PTIosAcceptScene(&state, &airplay, false));
    assert(!PTIosAcceptScene(&state, nullptr, true));
    assert(!PTIosIsPrimaryScene(&state, &airplay));
    assert(PTIosAcceptScene(&state, &phone, true));
    assert(PTIosBeginMain(&state));
    assert(PTIosIsPrimaryScene(&state, &phone));
    // AirPlay connect/disconnect cycles neither launch nor background the game.
    for (int i = 0; i < 3; ++i) {
        assert(!PTIosAcceptScene(&state, &airplay, false));
        assert(!PTIosIsPrimaryScene(&state, &airplay));
        assert(!PTIosBeginMain(&state));
        assert(PTIosIsPrimaryScene(&state, &phone));
    }
    assert(!PTIosAcceptScene(&state, &another_window, true));
    assert(!PTIosIsPrimaryScene(&state, &another_window));
    // A duplicate launch callback for the primary scene is harmless too.
    assert(PTIosAcceptScene(&state, &phone, true));
    assert(!PTIosBeginMain(&state));
}
