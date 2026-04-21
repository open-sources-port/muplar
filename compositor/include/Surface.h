#pragma once

#include <SDL2/SDL.h>

class Surface {
public:
    virtual ~Surface() = default;

    virtual void draw(SDL_Renderer* renderer) = 0;
};
