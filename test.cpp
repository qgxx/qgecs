#include "ecs.hpp"

#include <string>
#include <iostream>

struct Name {
    std::string name;
};

struct ID {
    int id;
};

struct Timer {
    int time;
};

void StartUpSystem(ecs::Commands& command) {
    command.Spawn<Name>(Name{ "person1" })
    .Spawn<Name, ID>(Name{ "person2" }, ID{ 1 })
    .Spawn<ID>(ID{ 2 })
    .Spawn<ID, Name>(ID{ 3 }, Name{ "person3" });
}

void EchoNameSystem(ecs::Commands& command, ecs::Queryer queryer, ecs::Resources resources, ecs::Events& events) {
    for (auto e : queryer.Query<Name>()) {
        std::cout << queryer.Get<Name>(e).name << std::endl;
    }
}

void EchoIDSystem(ecs::Commands& command, ecs::Queryer queryer, ecs::Resources resources, ecs::Events& events) {
    for (auto e : queryer.Query<ID>()) {
        std::cout << queryer.Get<ID>(e).id << std::endl;
    }

    events.Writer<std::string>().Write("From EchoIDSystem()");
}

void EchoTimerSystem(ecs::Commands& command, ecs::Queryer queryer, ecs::Resources resources, ecs::Events& events) {
    if (resources.Has<Timer>()) {
        auto& timer = resources.Get<Timer>();
        std::cout << timer.time << std::endl;
    }

    auto reader = events.Reader<std::string>();
    if (reader.Has()) {
        std::cout << reader.Read() << std::endl;
    }
}

void EchoNameAndIDSystem(ecs::Commands& command, ecs::Queryer queryer, ecs::Resources resources, ecs::Events& events) {
    for (auto e : queryer.Query<Name, ID>()) {
        std::cout << queryer.Get<ID>(e).id << ", " << queryer.Get<Name>(e).name << std::endl;
    }
}

int main() {
    ecs::World world;
    world.AddStartupSystem(StartUpSystem)
    .SetResources<Timer>(Timer{ 2002 })
    .AddSystem(EchoNameSystem)
    .AddSystem(EchoIDSystem)
    .AddSystem(EchoNameAndIDSystem)
    .AddSystem(EchoTimerSystem);

    world.Startup();

    world.Update();
    world.Update();

    world.Shutdown();

    return 0;
}