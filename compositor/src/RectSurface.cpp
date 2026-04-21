// compositor/src/RectSurface.cpp
#include "Surface.h"

class RectSurface : public Surface {
public:
    void draw(SDL_Renderer* renderer) override {
        SDL_Rect rect = {0, 0, 200, 150};

        SDL_SetRenderDrawColor(renderer, 0, 200, 255, 255);
        SDL_RenderFillRect(renderer, &rect);
    }
};
