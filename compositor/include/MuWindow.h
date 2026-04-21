#pragma once

#include <memory>
#include "Surface.h"

class MuWindow {
public:
    MuWindow(int x, int y, int w, int h, std::shared_ptr<Surface> surface);

    void draw(SDL_Renderer* renderer);

private:
    int x_, y_, width_, height_;
    std::shared_ptr<Surface> surface_;
};
