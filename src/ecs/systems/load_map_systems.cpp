#include "load_map_systems.h"
#include <SDL_image.h>
#include <box2d/b2_body.h>
#include <ecs/components/all_components.h>
#include <fstream>
#include <glm/fwd.hpp>
#include <memory>
#include <my_common_cpp_utils/Logger.h>
#include <my_common_cpp_utils/MathUtils.h>
#include <nlohmann/json.hpp>
#include <utils/sdl_RAII.h>

namespace
{

std::shared_ptr<Texture> LoadTexture(SDL_Renderer* renderer, const std::string& filePath)
{
    SDL_Texture* texture = IMG_LoadTexture(renderer, filePath.c_str());

    if (texture == nullptr)
        throw std::runtime_error("Failed to load texture");

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

std::shared_ptr<Box2dObjectRAII> CreateStaticPhysicsBody(
    std::shared_ptr<b2World> physicsWorld, const glm::u32vec2& position, const glm::u32vec2& size)
{
    b2BodyDef bodyDef;
    bodyDef.type = b2_staticBody;
    bodyDef.position.Set(position.x, position.y);
    b2Body* body = physicsWorld->CreateBody(&bodyDef);

    b2PolygonShape shape;
    shape.SetAsBox(size.x / 2.0, size.y / 2.0);

    b2FixtureDef fixtureDef;
    fixtureDef.shape = &shape;
    fixtureDef.density = 1.0f; // Density to calculate mass
    fixtureDef.friction = 0.3f; // Friction to apply to the body
    body->CreateFixture(&fixtureDef);

    return std::make_shared<Box2dObjectRAII>(body, physicsWorld);
}

std::shared_ptr<Box2dObjectRAII> CreateDynamicPhysicsBody(
    std::shared_ptr<b2World> physicsWorld, const glm::u32vec2& position, const glm::u32vec2& size)
{
    auto staticBody = CreateStaticPhysicsBody(physicsWorld, position, size);
    staticBody->GetBody()->SetType(b2_dynamicBody);
    return staticBody;
}

} // namespace

void UnloadMap(entt::registry& registry)
{
    // Remove all entities that have a TileInfo component.
    for (auto entity : registry.view<TileInfo>())
        registry.destroy(entity);

    // Remove all entities that have a PhysicalBody component.
    for (auto entity : registry.view<PhysicalBody>())
        registry.destroy(entity);

    if (Box2dObjectRAII::GetBodyCounter() != 0)
        MY_LOG_FMT(warn, "There are still {} Box2D bodies in the memory", Box2dObjectRAII::GetBodyCounter());
    else
        MY_LOG(info, "All Box2D bodies were destroyed");
}

void LoadMap(entt::registry& registry, SDL_Renderer* renderer, const std::string& filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
        throw std::runtime_error("Failed to open map file");

    nlohmann::json json;
    file >> json;

    // Calc path to tileset image.
    std::string tilesetPath = filename;
    size_t found = tilesetPath.find_last_of("/\\");
    if (found != std::string::npos)
        tilesetPath = tilesetPath.substr(0, found + 1);
    tilesetPath += json["tilesets"][0]["image"];

    // Check if the tileset texture file exists.
    std::ifstream tilesetFile(tilesetPath);
    if (!tilesetFile.is_open())
        throw std::runtime_error(MY_FMT("Failed to open tileset file {}", tilesetPath));

    // Load the tileset texture.
    auto tilesetTexture = LoadTexture(renderer, tilesetPath);
    int firstGid = json["tilesets"][0]["firstgid"];

    // Assume all tiles are of the same size.
    int tileWidth = json["tilewidth"];
    int tileHeight = json["tileheight"];

    // Calculate mini tile size: 4x4 mini tiles in one big tile.
    const int colAndRowNumber = 2;
    const int miniWidth = tileWidth / colAndRowNumber;
    const int miniHeight = tileHeight / colAndRowNumber;

    // Get the physics world.
    auto& gameState = registry.get<GameState>(registry.view<GameState>().front());
    auto physicsWorld = gameState.physicsWorld;

    // Iterate over each tile layer.
    size_t createdTiles = 0;
    for (const auto& layer : json["layers"])
    {
        if (layer["type"] == "tilelayer")
        {
            int layerCols = layer["width"];
            int layerRows = layer["height"];
            const auto& tiles = layer["data"];

            // Create entities for each tile.
            for (int layerRow = 0; layerRow < layerRows; ++layerRow)
            {
                for (int layerCol = 0; layerCol < layerCols; ++layerCol)
                {
                    int tileId = tiles[layerCol + layerRow * layerCols];

                    // Skip empty tiles.
                    if (tileId <= 0)
                        continue;

                    SDL_Rect textureSrcRect = CalculateSrcRect(tileId, tileWidth, tileHeight, tilesetTexture);

                    // Create entities for each mini tile inside the tile.
                    for (int miniRow = 0; miniRow < colAndRowNumber; ++miniRow)
                    {
                        for (int miniCol = 0; miniCol < colAndRowNumber; ++miniCol)
                        {
                            SDL_Rect miniTextureSrcRect{
                                textureSrcRect.x + miniCol * miniWidth, textureSrcRect.y + miniRow * miniHeight,
                                miniWidth, miniHeight};

                            glm::u32vec2 miniTileWorldPosition(
                                layerCol * tileWidth + miniCol * miniWidth,
                                layerRow * tileHeight + miniRow * miniHeight);
                            auto entity = registry.create();
                            registry.emplace<Angle>(entity);
                            registry.emplace<Position>(entity);
                            registry.emplace<SizeComponent>(entity, glm::vec2(miniWidth, miniHeight));
                            registry.emplace<TileInfo>(entity, tilesetTexture, miniTextureSrcRect);

                            auto tilePhysicsBody =
                                CreateStaticPhysicsBody(physicsWorld, miniTileWorldPosition, {miniWidth, miniHeight});

                            // Apply randomly: static/dynamic body.
                            tilePhysicsBody->GetBody()->SetType(
                                utils::randomTrue(0.1f) ? b2_dynamicBody : b2_staticBody);

                            registry.emplace<PhysicalBody>(entity, tilePhysicsBody);

                            createdTiles++;
                        }
                    }
                }
            }
        }
        else if (layer["type"] == "objectgroup")
        {
            for (const auto& object : layer["objects"])
            {
                if (object["type"] == "PlayerPosition")
                {
                    auto entity = registry.create();
                    registry.emplace<Angle>(entity);
                    registry.emplace<Position>(entity);
                    auto playerSize = glm::u32vec2(32, 32);
                    registry.emplace<SizeComponent>(entity, playerSize);
                    registry.emplace<PlayerNumber>(entity);

                    auto playerPhysicsBody =
                        CreateDynamicPhysicsBody(physicsWorld, glm::u32vec2(object["x"], object["y"]), playerSize);
                    registry.emplace<PhysicalBody>(entity, playerPhysicsBody);
                }
            }
        }
    }

    if (createdTiles == 0)
        throw std::runtime_error(MY_FMT("No tiles were created during map loading {}", filename));
}
