#include "texture_process.h"
#include <SDL_image.h>
#include <my_common_cpp_utils/Logger.h>

bool IsTileInvisible(std::shared_ptr<Texture> tilesetTexture, const SDL_Rect& miniTextureSrcRect)
{
    Uint32* pixels;
    int pitch; // The length of a row of pixels in bytes.

    if (SDL_LockTexture(tilesetTexture->get(), &miniTextureSrcRect, (void**)&pixels, &pitch) != 0)
    {
        MY_LOG_FMT(
            warn,
            "SDL_LockTexture failed. Check that you use function LoadTextureWithStreamingAccess to load texture. Error: {}",
            SDL_GetError());
        return true;
    }

    bool isTileInvicible = true;
    for (int row = 0; row < miniTextureSrcRect.h; ++row)
    {
        for (int col = 0; col < miniTextureSrcRect.w; ++col)
        {
            static const int pixelSize = 4; // 4 bytes per pixel (ABGR).
            // TODO: here is a bug. pixel is always zero. Probably the issue in LoadTextureWithStreamingAccess.
            Uint32 pixel = pixels[row * (pitch / pixelSize) + col];
            Uint8 alpha = pixel & 0xFF; // Correct for SDL_PIXELFORMAT_ABGR8888
            // MY_LOG_FMT(info, "Alpha: {}, Pixel: {}, Pitch: {}, col: {}, row: {}", alpha, pixel, pitch, col, row);
            if (alpha > 0)
            {
                isTileInvicible = false;
                break;
            }
        }
        if (isTileInvicible)
        {
            break;
        }
    }

    SDL_UnlockTexture(tilesetTexture->get());
    return isTileInvicible;
}

std::shared_ptr<Texture> LoadTexture(SDL_Renderer* renderer, const std::string& filePath)
{
    SDL_Texture* texture = IMG_LoadTexture(renderer, filePath.c_str());

    if (texture == nullptr)
        throw std::runtime_error("Failed to load texture");

    return std::make_shared<Texture>(texture);
}
std::shared_ptr<Texture> LoadTextureWithStreamingAccess(SDL_Renderer* renderer, const std::string& filePath)
{
    // Step 1. Load image into SDL_Surface.
    SDL_Surface* surface = IMG_Load(filePath.c_str());
    if (!surface)
        throw std::runtime_error(MY_FMT("Failed to load image {}. Error: {}", filePath, IMG_GetError()));

    MY_LOG_FMT(
        info, "Surface format: {}, w: {}, h: {}", SDL_GetPixelFormatName(surface->format->format), surface->w,
        surface->h);

    // Step 2: Create a texture with the SDL_TEXTUREACCESS_STREAMING flag.
    SDL_Texture* texture =
        SDL_CreateTexture(renderer, surface->format->format, SDL_TEXTUREACCESS_STREAMING, surface->w, surface->h);
    if (!texture)
    {
        SDL_FreeSurface(surface);
        throw std::runtime_error(
            MY_FMT("Failed to create streaming texture for image {}. Error: {}", filePath, SDL_GetError()));
    }

    // Step 3. Copy pixel data from the surface to the texture.
    if (SDL_UpdateTexture(texture, nullptr, surface->pixels, surface->pitch) != 0)
    {
        MY_LOG_FMT(warn, "SDL_UpdateTexture failed. Error: {}", SDL_GetError());
    }

    // Step 4. Free the surface.
    SDL_FreeSurface(surface);

    return std::make_shared<Texture>(texture);
}
SDL_Rect CalculateSrcRect(int tileId, int tileWidth, int tileHeight, std::shared_ptr<Texture> texture)
{
    int textureWidth, textureHeight;
    SDL_QueryTexture(texture->get(), nullptr, nullptr, &textureWidth, &textureHeight);

    int tilesPerRow = textureWidth / tileWidth;
    tileId -= 1; // Adjust tileId to match 0-based indexing.

    SDL_Rect srcRect;
    srcRect.x = (tileId % tilesPerRow) * tileWidth;
    srcRect.y = (tileId / tilesPerRow) * tileHeight;
    srcRect.w = tileWidth;
    srcRect.h = tileHeight;

    return srcRect;
}
