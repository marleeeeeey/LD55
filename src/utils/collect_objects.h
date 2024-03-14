#pragma once
#include "utils/game_options.h"
#include <SDL.h>
#include <box2d/box2d.h>
#include <entt/entt.hpp>
#include <optional>
#include <utils/factories/objects_factory.h>

class CollectObjects
{
    entt::registry& registry;
    GameOptions& gameState;
    ObjectsFactory& objectsFactory;
public:
    CollectObjects(entt::registry& registry, ObjectsFactory& objectsFactory);
public:
    std::vector<entt::entity> GetPhysicalBodiesInRaduis(
        const b2Vec2& center, float radius, std::optional<b2BodyType> bodyType);
    std::vector<entt::entity> GetPhysicalBodiesInRaduis(
        const std::vector<entt::entity>& entities, const b2Vec2& center, float physicalRadius,
        std::optional<b2BodyType> bodyType);
public:
    std::vector<entt::entity> ExcludePlayersFromList(const std::vector<entt::entity>& entities);
};