#pragma once

#include <cassert>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace game::ecs
{

using Entity = std::uint32_t;
inline constexpr Entity kNullEntity = std::numeric_limits<std::uint32_t>::max();

namespace detail
{

inline std::size_t nextComponentTypeId() noexcept
{
    static std::size_t counter = 0;
    return counter++;
}

template <typename T>
std::size_t componentTypeId() noexcept
{
    static const std::size_t id = nextComponentTypeId();
    return id;
}

class IPool
{
public:
    virtual ~IPool() = default;
    virtual void remove(Entity entity) = 0;
};

template <typename T>
class SparseSet final : public IPool
{
public:
    template <typename... Args>
    T &emplace(Entity entity, Args &&...args)
    {
        if (contains(entity))
        {
            T &existing = m_dense[m_sparse[entity]];
            existing = T(std::forward<Args>(args)...);
            return existing;
        }
        if (entity >= m_sparse.size())
        {
            m_sparse.resize(static_cast<std::size_t>(entity) + 1, kNullEntity);
        }
        const auto denseIdx = static_cast<std::uint32_t>(m_dense.size());
        m_sparse[entity] = denseIdx;
        m_entities.push_back(entity);
        m_dense.emplace_back(std::forward<Args>(args)...);
        return m_dense.back();
    }

    [[nodiscard]] bool contains(Entity entity) const noexcept
    {
        return entity < m_sparse.size() && m_sparse[entity] != kNullEntity;
    }

    [[nodiscard]] T &get(Entity entity) noexcept
    {
        assert(contains(entity));
        return m_dense[m_sparse[entity]];
    }

    [[nodiscard]] const T &get(Entity entity) const noexcept
    {
        assert(contains(entity));
        return m_dense[m_sparse[entity]];
    }

    void remove(Entity entity) override
    {
        if (!contains(entity))
        {
            return;
        }
        const std::uint32_t idx = m_sparse[entity];
        const std::uint32_t lastIdx = static_cast<std::uint32_t>(m_dense.size() - 1);
        if (idx != lastIdx)
        {
            const Entity movedEntity = m_entities[lastIdx];
            m_dense[idx] = std::move(m_dense[lastIdx]);
            m_entities[idx] = movedEntity;
            m_sparse[movedEntity] = idx;
        }
        m_dense.pop_back();
        m_entities.pop_back();
        m_sparse[entity] = kNullEntity;
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_dense.size(); }
    [[nodiscard]] const std::vector<Entity> &entities() const noexcept { return m_entities; }
    [[nodiscard]] std::vector<T> &dense() noexcept { return m_dense; }

private:
    std::vector<T> m_dense;
    std::vector<Entity> m_entities;
    std::vector<std::uint32_t> m_sparse;
};

} // namespace detail

/// Minimal sparse-set ECS registry (~130 lines, O(1) lookup/remove, contiguous component iteration).
class Registry
{
public:
    [[nodiscard]] Entity create()
    {
        if (!m_freeList.empty())
        {
            const Entity id = m_freeList.back();
            m_freeList.pop_back();
            return id;
        }
        return m_nextEntity++;
    }

    void destroy(Entity entity)
    {
        for (const auto &pool : m_pools)
        {
            if (pool != nullptr)
            {
                pool->remove(entity);
            }
        }
        m_freeList.push_back(entity);
    }

    template <typename T, typename... Args>
    T &emplace(Entity entity, Args &&...args)
    {
        return ensurePool<T>().emplace(entity, std::forward<Args>(args)...);
    }

    template <typename T>
    [[nodiscard]] bool has(Entity entity) const noexcept
    {
        const auto *pool = findPool<T>();
        return pool != nullptr && pool->contains(entity);
    }

    template <typename T>
    [[nodiscard]] T &get(Entity entity) noexcept
    {
        auto *pool = findPool<T>();
        assert(pool != nullptr);
        return pool->get(entity);
    }

    template <typename T>
    [[nodiscard]] T *tryGet(Entity entity) noexcept
    {
        auto *pool = findPool<T>();
        return (pool != nullptr && pool->contains(entity)) ? &pool->get(entity) : nullptr;
    }

    template <typename T>
    void remove(Entity entity)
    {
        if (auto *pool = findPool<T>())
        {
            pool->remove(entity);
        }
    }

    template <typename Primary, typename... Rest, typename Fn>
    void each(Fn &&fn)
    {
        auto *primaryPool = findPool<Primary>();
        if (primaryPool == nullptr || primaryPool->size() == 0)
        {
            return;
        }
        if constexpr (sizeof...(Rest) > 0)
        {
            if (((findPool<Rest>() == nullptr) || ...))
            {
                return;
            }
        }

        const auto &entities = primaryPool->entities();
        auto &dense = primaryPool->dense();
        for (std::size_t i = 0; i < entities.size(); ++i)
        {
            const Entity e = entities[i];
            if constexpr (sizeof...(Rest) == 0)
            {
                fn(e, dense[i]);
            }
            else if ((has<Rest>(e) && ...))
            {
                fn(e, dense[i], get<Rest>(e)...);
            }
        }
    }

private:
    template <typename T>
    [[nodiscard]] detail::SparseSet<T> &ensurePool()
    {
        const std::size_t typeId = detail::componentTypeId<T>();
        if (typeId >= m_pools.size())
        {
            m_pools.resize(typeId + 1);
        }
        if (m_pools[typeId] == nullptr)
        {
            m_pools[typeId] = std::make_unique<detail::SparseSet<T>>();
        }
        return *static_cast<detail::SparseSet<T> *>(m_pools[typeId].get());
    }

    template <typename T>
    [[nodiscard]] detail::SparseSet<T> *findPool() const noexcept
    {
        const std::size_t typeId = detail::componentTypeId<T>();
        if (typeId >= m_pools.size())
        {
            return nullptr;
        }
        return static_cast<detail::SparseSet<T> *>(m_pools[typeId].get());
    }

    Entity m_nextEntity = 0;
    std::vector<Entity> m_freeList;
    std::vector<std::unique_ptr<detail::IPool>> m_pools;
};

} // namespace game::ecs
