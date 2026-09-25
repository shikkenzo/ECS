#pragma once

#include "EntityID.h"
#include <vector>
#include <utility>
#include <cstddef>



constexpr size_t ENTITY_PAGE_SIZE = 32;
constexpr size_t NULL_INDEX = SIZE_MAX;

extern int s_componentCounter;

template <class T>
int GetComponentTypeId()
{
    static int s_componentId = s_componentCounter++;
    return s_componentId;
}

struct IComponentPool
{
    virtual ~IComponentPool() = default;
    virtual void Remove(EntityIndex index) = 0;
    virtual bool Contains(EntityIndex index) const = 0;
    virtual size_t GetSize() const = 0;
    virtual EntityIndex GetEntityIndex(size_t denseIndex) const = 0;
};

template <class T>
struct ComponentPool : IComponentPool
{
    template<typename... Args>
    T& Add(EntityIndex index, Args&&... args)
    {
        if (auto* existing = Get(index))
            return *existing;

        SetInSparse(index, m_dense.size());
        m_dense.emplace_back(std::forward<Args>(args)...);
        m_denseToEntity.push_back(index);
        return m_dense.back();
    }

    T* Get(EntityIndex index)
    {
        size_t i = GetInSparse(index);
        return i == NULL_INDEX ? nullptr : &m_dense[i];
    }

    const T* Get(EntityIndex index) const
    {
        size_t i = GetInSparse(index);

        if (i == NULL_INDEX)
            return nullptr;

        return &m_dense[i];
    }

    void Remove(EntityIndex index) override
    {
        size_t i = GetInSparse(index);
        if (i == NULL_INDEX)
            return;

        size_t backIndex = m_dense.size() - 1;
        if (i != backIndex)
        {
            EntityIndex backEntity = m_denseToEntity[backIndex];
            m_dense[i] = std::move(m_dense.back());
            m_denseToEntity[i] = backEntity;
            SetInSparse(backEntity, i);
        }

        m_dense.pop_back();
        m_denseToEntity.pop_back();
        SetInSparse(index, NULL_INDEX);
    }

    bool Contains(EntityIndex index) const override
    {
        return GetInSparse(index) != NULL_INDEX;
    }

    size_t GetSize() const override { return m_dense.size(); }

    EntityIndex GetEntityIndex(size_t denseIndex) const override
    {
        return m_denseToEntity[denseIndex];
    }

private:
    size_t GetInSparse(EntityIndex index) const
    {
        const size_t page = index / ENTITY_PAGE_SIZE;
        const size_t offset = index % ENTITY_PAGE_SIZE;

        if (page >= m_sparse.size() || offset >= m_sparse[page].size())
            return NULL_INDEX;

        return m_sparse[page][offset];
    }

    void SetInSparse(EntityIndex index, size_t value)
    {
        const size_t page = index / ENTITY_PAGE_SIZE;
        const size_t offset = index % ENTITY_PAGE_SIZE;

        if (page >= m_sparse.size())
            m_sparse.resize(page + 1);

        if (offset >= m_sparse[page].size())
            m_sparse[page].resize(ENTITY_PAGE_SIZE, NULL_INDEX);

        m_sparse[page][offset] = value;
    }

    std::vector<T> m_dense;
    std::vector<std::vector<size_t>> m_sparse;
    std::vector<EntityIndex> m_denseToEntity;
};
