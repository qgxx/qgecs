#ifndef __ECS_H__
#define __ECS_H__

#include "sparse_set.hpp"
#include <vector>
#include <algorithm>
#include <cassert>
#include <unordered_map>

#define assertm(msg, expr) assert(((void)(msg), (expr)))

namespace ecs {

using ComponentID = uint32_t;
using Entity = uint32_t;

struct Resource{};
struct Component{};

template <typename Category>
class IndexGetter final {
public:
    template <typename T>
    static uint32_t Get() {
        static uint32_t id = curIdx_++;
        return id;
    }

private:
    inline static uint32_t curIdx_ = 0;

};

template <typename T, typename = std::enable_if<std::is_integral_v<T>>>
struct IDGenerator {
public:
    static T Generator() {
        return curId_++;
    }

private:
    inline static T curId_ = {};

};

using EntityGenerator = IDGenerator<Entity>;

class Commands;
class Resources;
class Queryer;

using UpdateSystem = void(*)(Commands, Queryer, Resources);
using StartupSystem = void(*)(Commands);

class World final {
public:
    friend class Commands;
    friend class Resources;
    friend class Queryer;

    using ComponentContainer = std::unordered_map<ComponentID, void*>;

    World() = default;
    World(const World&) = delete;
    World& operator= (const World&) = delete;

    World& AddStartupSystem(StartupSystem system) {
        startupSystems_.push_back(system);
        return *this;
    }

    World& AddSystem(UpdateSystem system) {
        updateSystems_.push_back(system);
        return *this;
    }

    void Startup();
    void Update();
    void Shutdown() {
        entities_.clear();
        resource_.clear();
        componentMap_.clear();
    }

    template <typename T>
    World& SetResources(T&& resource);

private:
    struct Pool final { 
        std::vector<void*> instances;
        std::vector<void*> cache;

        using CreateFunc = void*(*)(void);
        using DestroyFunc = void(*)(void*);

        CreateFunc create;
        DestroyFunc destroy;

        Pool(CreateFunc create, DestroyFunc destroy) : create { create }, destroy { destroy } {
            assertm("You must give a not-null create function", create);
            assertm("You must give a not-null destroy function", destroy);
        }

        void* Create() {
            if (!cache.empty()) {
                instances.push_back(cache.back());
                cache.pop_back();
            }
            else {
                instances.push_back(create());
            }
            return instances.back();
        }

        void Destroy(void* elem) {
            if (auto it = std::find(instances.begin(), instances.end(), elem); it != instances.end()) {
                cache.push_back(*it);
                std::swap(*it, instances.back());
                instances.pop_back();
            }
            else {
                assertm("Element not in pool!", false);
            }
        }
    };

    struct ComponentInfo {
        Pool pool;
        sparse_set<Entity, 32> sparseSet;

        ComponentInfo(Pool::CreateFunc create, Pool::DestroyFunc destory) : pool{create, destory} {}
        ComponentInfo() : pool{nullptr, nullptr} {}
    };

    using ComponentMap = std::unordered_map<ComponentID, ComponentInfo>;
    ComponentMap componentMap_;
    std::unordered_map<Entity, ComponentContainer> entities_;

    struct ResourceInfo {
        void* resource = nullptr;

        using CreateFunc = void*(*)(void);
        using DestroyFunc = void(*)(void*);

        CreateFunc create = nullptr;
        DestroyFunc destroy = nullptr;

        ResourceInfo(CreateFunc create, DestroyFunc destroy) : create { create }, destroy { destroy } {
            assertm("You must give a not-null create function", create);
            assertm("You must give a not-null destroy function", destroy);
        }
        ResourceInfo() = default;
        ~ResourceInfo() {
            destroy(resource);
        }
    };

    std::unordered_map<ComponentID, ResourceInfo> resource_;

    std::vector<StartupSystem> startupSystems_;
    std::vector<UpdateSystem> updateSystems_;
};

class Commands final {
public:
    Commands(World& world) : world_ { world } {}

    template <typename... ComponentTypes>
    Commands& Spawn(ComponentTypes&&... components) {
        Entity entity = EntityGenerator::Generator();
        doSpawn(entity, std::forward<ComponentTypes>(components)...);
        return *this;
    }

    template <typename... ComponentTypes>
    Entity Spawn_r(ComponentTypes&&... components) {
        Entity entity = EntityGenerator::Generator();
        doSpawn(entity, std::forward<ComponentTypes>(components)...);
        return entity;
    }

    Commands& Destroy(Entity entity) {
        if (auto it = world_.entities_.find(entity); it != world_.entities_.end()) {
            for (auto [id, component] : it->second) {
                auto& componentInfo = world_.componentMap_[id];
                componentInfo.pool.Destroy(component);
                componentInfo.sparseSet.remove(entity);
            }
            world_.entities_.erase(it);
        }
        return *this;
    }

    template <typename T> 
    Commands& SetResource(T&& resource) {
        auto index = IndexGetter<Resource>::Get<T>();
        if (auto it = world_.resource_.find(index); it != world_.resource_.end()) {
            assertm("resource already exists", it->second.resource);
            it->second.resource = new T(std::forward<T>(resource));
        }
        else {
            auto newIt = world_.resource_.emplace(index, World::ResourceInfo(
                []()->void* { return new T; },
                [](void* elem) { delete (T*)(elem); }
            ));
            newIt.first->second.resource = new T;
            *((T*)newIt.first->second.resource) = std::forward<T>(resource);
        }
        return *this;
    }

    template <typename T>
    Commands& RemoveResource() {
        auto index = IndexGetter<Resource>::Get<T>();
        if (auto it = world_.resource_.find(index); it != world_.resource_.end()) {
            delete (T*)it->second.resource;
            it->second.resource = nullptr;
        }
        return *this;
    }

private:
    World& world_;

    template <typename T, typename... Remains>
    void doSpawn(Entity entity, T&& component, Remains&&... remains) {
        auto index = IndexGetter<Component>::Get<T>();
        if (auto it = world_.componentMap_.find(index); it == world_.componentMap_.end()) {
            world_.componentMap_.emplace(index, World::ComponentInfo(
                []()->void* { return new T; },
                [](void* elem) { delete (T*)(elem); }
            ));
        }
        World::ComponentInfo& componentInfo = world_.componentMap_[index];
        void* elem = componentInfo.pool.Create();
        *((T*)elem) = std::forward<T>(component);
        componentInfo.sparseSet.add(entity);

        auto it = world_.entities_.emplace(entity, World::ComponentContainer{});
        it.first->second[index] = elem;

        if constexpr (sizeof...(remains) != 0) {
            doSpawn<Remains...>(entity, std::forward<Remains>(remains)...);
        }
    }
};

class Resources final {
public:
    Resources(World& world) : world_ { world } {}

    template <typename T>
    bool Has() {
        auto index = IndexGetter<Resource>::Get<T>();
        auto it = world_.resource_.find(index);
        return (it != world_.resource_.end() && it->second.resource);
    }

    template <typename T>
    T& Get() {
        auto index = IndexGetter<Resource>::Get<T>();
        return *((T*)world_.resource_[index].resource);
    }

private:
    World& world_;

};

class Queryer final {
public:
    Queryer(World& world) : world_ { world } {}

    template <typename... Components>
    std::vector<Entity> Query() {
        std::vector<Entity> entities;
        doQuery<Components...>(entities);
        return entities;
    }

    template <typename T>
    bool Has(Entity entity) {
        auto index = IndexGetter<Component>::Get<T>();
        auto it = world_.entities_.find(entity);
        return (it != world_.entities_.end() && it->second.find(index) != it->second.end());
    }

    template <typename T>
    T& Get(Entity entity) {
        auto index = IndexGetter<Component>::Get<T>();
        return *((T*)world_.entities_[entity][index]);
    }


private:
    World& world_;

    template <typename T, typename... Remains>
    void doQuery(std::vector<Entity>& entities) {
        auto index = IndexGetter<Component>::Get<T>();
        World::ComponentInfo& info = world_.componentMap_[index];
        for (auto e : info.sparseSet) {
            if constexpr (sizeof...(Remains) != 0) {
                doQueryRemains<Remains...>(e, entities);
            }
            else {
                entities.push_back(e);
            }
        }
    }

    template <typename T, typename... Remains>
    void doQueryRemains(Entity entity, std::vector<Entity>& entities) {
        auto index = IndexGetter<Component>::Get<T>();
        auto& componentContainer = world_.entities_[entity];
        if (auto it = componentContainer.find(index); it == componentContainer.end()) {
            return;
        }

        if constexpr (sizeof...(Remains) == 0) {
            entities.push_back(entity);
            return;
        }
        else {
            return doQueryRemains<Remains...>(entity, entities);
        }
    }
};

inline void World::Startup() {
    for (auto sys : startupSystems_) {
        sys(Commands{*this});
    }
}

inline void World::Update() {
    for (auto sys : updateSystems_) {
        sys(Commands{*this}, Queryer{*this}, Resources{*this});
    }
}

template <typename T>
inline World& World::SetResources(T&& resource) {
    Commands commands(*this);
    commands.SetResource(std::forward<T>(resource));
    return *this;
}

} // namespace ecs

#endif // !__ECS_H__  