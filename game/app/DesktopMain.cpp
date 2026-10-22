#include "game/app/OpenYammMain.h"

#include <SDL3/SDL.h>

int main(int argc, char **argv)
{
    SDL_SetAppMetadata("OpenYAMM", nullptr, "io.github.openyamm.OpenYAMM");
    return OpenYAMM::Game::runApplication(argc, argv);
}
