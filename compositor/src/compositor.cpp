#include "Compositor.h"
#include <SDL2/SDL.h>
#include <iostream>
#include <vector>
#include <memory>
#include "Surface.h"      // ✅ REQUIRED
#include "MuWindow.h"     // ✅ REQUIRED

// Temporary surface
class RectSurface : public Surface {
public:
    void draw(SDL_Renderer* renderer) override {
        SDL_Rect rect = {0, 0, 200, 150};
        SDL_SetRenderDrawColor(renderer, 0, 200, 255, 255);
        SDL_RenderFillRect(renderer, &rect);
    }
};

static SDL_Window* window = nullptr;
static SDL_Renderer* renderer = nullptr;

bool Compositor::init() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << std::endl;
        return false;
    }

    window = SDL_CreateWindow(
        "Muplar",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        800, 600,
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        std::cerr << "Window Error: " << SDL_GetError() << std::endl;
        return false;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    if (!renderer) {
        std::cerr << "Renderer Error: " << SDL_GetError() << std::endl;
        return false;
    }

    return true;
}

void Compositor::run() {
    std::vector<std::shared_ptr<MuWindow>> windows;

    windows.push_back(std::make_shared<MuWindow>(
        200, 150, 400, 300,
        std::make_shared<RectSurface>()
    ));

    bool running = true;
    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
        }

        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_RenderClear(renderer);

        // SDL_Rect rect = {200, 150, 400, 300};
        // SDL_SetRenderDrawColor(renderer, 0, 200, 255, 255);
        // SDL_RenderFillRect(renderer, &rect);
        for (auto& win : windows) {
            win->draw(renderer);
        }

        SDL_RenderPresent(renderer);
    }
}

void Compositor::shutdown() {
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
