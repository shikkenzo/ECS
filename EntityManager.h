#pragma once

#include "EntityID.h"
#include "ComponentPool.h"

#include <array>
#include <cassert>
#include <memory>
#include <tuple>
#include <vector>
#include <utility>



class EntityManager
{
public:
    EntityID CreateEntity()
    {
        if (!m_freeEntities.empty())
        {
            EntityIndex index = m_freeEntities.back();
            m_freeEntities.pop_back();

            EntityID id = CreateEntityID(index, GetEntityVersion(m_entities[index].id));
            m_entities[index].id = id;
            return id;
        }

        EntityIndex index = static_cast<EntityIndex>(m_entities.size());
        m_entities.push_back({ CreateEntityID(index, 0) });
        return m_entities.back().id;
    }

    void DestroyEntity(EntityID entity)
    {
        if (!IsEntityAlive(entity))
            return;

        EntityIndex index = GetEntityIndex(entity);
        EntityVersion nextVersion = GetEntityVersion(entity) + 1;

        m_entities[index].id = CreateEntityID(EntityIndex(-1), nextVersion);
        m_freeEntities.push_back(index);

        for (auto& pool : m_componentPools)
            if (pool)
                pool->Remove(index);
    }

    bool IsEntityAlive(EntityID entity) const
    {
        EntityIndex index = GetEntityIndex(entity);
        return index < m_entities.size() && m_entities[index].id == entity;
    }

    size_t GetEntityCount() const { return m_entities.size(); }

    EntityID GetEntityByIndex(EntityIndex index) const
    {
        return index < m_entities.size() ? m_entities[index].id : INVALID_ENTITY;
    }

    template<typename T, typename... Args>
    T* AddComponent(EntityID entity, Args&&... args)
    {
        if (!IsEntityAlive(entity))
            return nullptr;

        int componentId = GetComponentTypeId<T>();
        if (m_componentPools.size() <= static_cast<size_t>(componentId))
            m_componentPools.resize(componentId + 1);

        if (!m_componentPools[componentId])
            m_componentPools[componentId] = std::make_unique<ComponentPool<T>>();

        auto* pool = static_cast<ComponentPool<T>*>(m_componentPools[componentId].get());
        return &pool->Add(GetEntityIndex(entity), std::forward<Args>(args)...);
    }

    template<typename T>
    T* GetComponent(EntityID entity)
    {
        if (!IsEntityAlive(entity))
            return nullptr;

        int componentId = GetComponentTypeId<T>();
        if (componentId >= static_cast<int>(m_componentPools.size()))
            return nullptr;

        auto* pool = static_cast<ComponentPool<T>*>(m_componentPools[componentId].get());
        if (!pool)
            return nullptr;

        return pool->Get(GetEntityIndex(entity));
    }

    template<typename T>
    const T* GetComponent(EntityID entity) const
    {
        return const_cast<EntityManager*>(this)->GetComponent<T>(entity);
    }

    template<typename T>
    void RemoveComponent(EntityID entity)
    {
        if (!IsEntityAlive(entity))
            return;

        int componentId = GetComponentTypeId<T>();
        if (componentId >= static_cast<int>(m_componentPools.size()))
            return;

        auto* pool = static_cast<ComponentPool<T>*>(m_componentPools[componentId].get());
        if (pool)
            pool->Remove(GetEntityIndex(entity));
    }

    template<typename T>
    IComponentPool* GetComponentPool()
    {
        size_t componentId = GetComponentTypeId<T>();
        if (componentId >= m_componentPools.size())
            return nullptr;

        return m_componentPools[componentId].get();
    }

    template<typename T>
    const IComponentPool* GetComponentPool() const
    {
        return const_cast<EntityManager*>(this)->GetComponentPool<T>();
    }

private:
    struct Entity { EntityID id; };

    std::vector<Entity> m_entities;
    std::vector<EntityIndex> m_freeEntities;
    std::vector<std::unique_ptr<IComponentPool>> m_componentPools;
};

template<typename... ComponentTypes>
class SceneView
{
public:
    explicit SceneView(EntityManager& entityManager)
        : m_entityManager(&entityManager)
    {
        size_t i = 0;
        ((m_pools[i++] = entityManager.GetComponentPool<ComponentTypes>()), ...);

        for (IComponentPool* pool : m_pools)
        {
            if (!pool)
            {
                m_valid = false;
                return;
            }

            if (!m_smallestPool || pool->GetSize() < m_smallestPool->GetSize())
                m_smallestPool = pool;
        }
    }

    struct Iterator
    {
        Iterator(EntityManager* manager,
                 const std::array<IComponentPool*, sizeof...(ComponentTypes)>* pools,
                 IComponentPool* smallest,
                 size_t index)
            : manager(manager), pools(pools), smallest(smallest), index(index) 
        {
            //assert(pools->size() > 0 || && (!smallestPool))) //!?
            iterateAll = ((pools->size() == 0) && (!smallest));
        }

        bool operator==(const Iterator& other) const { return index == other.index; }
        bool operator!=(const Iterator& other) const { return !(*this == other); }

        Iterator& operator++()
        {
            ++index;
            SkipInvalid();
            return *this;
        }

        auto operator*() const
        {
            if constexpr (sizeof...(ComponentTypes) == 0)
            {
                EntityID entity = manager->GetEntityByIndex(index);
                return std::tuple<EntityID>(entity);
            }
            else
            {
                EntityIndex entityIndex = smallest->GetEntityIndex(index);
                EntityID entity = manager->GetEntityByIndex(entityIndex);
                return std::tuple<EntityID, ComponentTypes&...>(entity, *static_cast<ComponentPool<ComponentTypes>*>(manager->GetComponentPool<ComponentTypes>())->Get(entityIndex)...);  //! I should use viewPools
            }
        }

    public:
        void SkipInvalid()
        {
            while (!IsAtEnd() && !IsAtValidIndex())
                ++index;
        }

        bool IsAtEnd() const
        {
            if (!iterateAll)
                return index >= smallest->GetSize();

            return index >= manager->GetEntityCount();
        }

        bool IsAtValidIndex() const
        {
            if (!iterateAll)
            {
                return IsAtValidViewIndex();
            }

            return IsAtValidSceneIndex();
        }

        bool IsAtValidSceneIndex() const
        {
            if (index >= manager->GetEntityCount())
                return false;

            EntityID entityID = manager->GetEntityByIndex(index);

            return IsEntityIDValid(entityID);
        }

        bool IsAtValidViewIndex() const
        {
            if (index >= smallest->GetSize())
                return false;

            EntityIndex entityIndex = smallest->GetEntityIndex(index);

            EntityID entityID = manager->GetEntityByIndex(entityIndex);

            for (size_t i = 0; i < pools->size(); ++i)
            {
                if (!(*pools)[i]->Contains(entityIndex))
                    return false;
            }

            return true;
        }

        EntityManager* manager;
        const std::array<IComponentPool*, sizeof...(ComponentTypes)>* pools;
        IComponentPool* smallest;
        size_t index;

        bool iterateAll = false;
    };

    Iterator begin() const
    {
        if (!m_valid)
            return end();

        Iterator it(m_entityManager, &m_pools, m_smallestPool, 0);
        it.SkipInvalid();
        return it;
    }

    Iterator end() const
    {
        if (m_smallestPool)
        {
            return Iterator(m_entityManager, &m_pools, m_smallestPool, m_smallestPool->GetSize());
        }

        return Iterator(m_entityManager, &m_pools, nullptr, m_entityManager->GetEntityCount());
    }

private:
    EntityManager* m_entityManager;
    std::array<IComponentPool*, sizeof...(ComponentTypes)> m_pools{};
    IComponentPool* m_smallestPool = nullptr;
    bool m_valid = true;
};
