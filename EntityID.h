#pragma once

#include <cstdint>



using EntityIndex = std::uint32_t;
using EntityVersion = std::uint32_t;
using EntityID = std::uint64_t;

constexpr EntityID CreateEntityID(EntityIndex index, EntityVersion version)
{
	return (static_cast<EntityID>(index) << 32) | static_cast<EntityID>(version);
}

constexpr EntityIndex GetEntityIndex(EntityID id)
{
	return static_cast<EntityIndex>(id >> 32);
}

constexpr EntityVersion GetEntityVersion(EntityID id)
{
	return static_cast<EntityVersion>(id);
}

inline bool IsEntityIDValid(EntityID id)
{
	return GetEntityIndex(id) != EntityIndex(-1);
}

#define INVALID_ENTITY CreateEntityID(EntityIndex(-1), 0)
