#include "MuWindow.h"

MuWindow::MuWindow(int x, int y, int w, int h, std::shared_ptr<Surface> surface)
    : x_(x), y_(y), width_(w), height_(h), surface_(surface) {}

void MuWindow::draw(SDL_Renderer* renderer) {
    // Move drawing origin (simple version)
    SDL_Rect viewport = {x_, y_, width_, height_};
    SDL_RenderSetViewport(renderer, &viewport);

    if (surface_) {
        surface_->draw(renderer);
    }

    // Reset viewport
    SDL_RenderSetViewport(renderer, nullptr);
}
