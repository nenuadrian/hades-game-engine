#include "engine/core/ecs/component_manager.hpp"
#include "engine/core/ecs/constants.h"
#include "engine/core/ecs/entity_manager.hpp"
#include "engine/core/ecs/system_manager.hpp"

#include <bitset>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
  struct Position
  {
    float x;
    float y;
    float z;
  };

  struct Velocity
  {
    float x;
    float y;
    float z;
  };

  struct BenchmarkConfig
  {
    std::size_t entities = 100000;
    std::size_t frames = 500;
    std::size_t iterations = 5;
    std::size_t warmup = 1;
  };

  struct BenchmarkResult
  {
    std::string name;
    double averageMilliseconds = 0.0;
    double minimumMilliseconds = 0.0;
    double maximumMilliseconds = 0.0;
    double operationsPerSecond = 0.0;
    std::string throughputUnit;
    double millisecondsPerFrame = 0.0;
    double guard = 0.0;
  };

  class BenchmarkMovementSystem : public hades::System
  {
  public:
    double accumulator = 0.0;

    void update(float deltaTime, hades::ComponentManager &componentManager, hades::EntityManager &entityManager) override
    {
      for (const auto entity : entityManager.getAllEntities())
      {
        if (!componentManager.hasComponent<Position>(entity) || !componentManager.hasComponent<Velocity>(entity))
        {
          continue;
        }

        auto &position = componentManager.getComponent<Position>(entity);
        auto &velocity = componentManager.getComponent<Velocity>(entity);

        position.x += velocity.x * deltaTime;
        position.y += velocity.y * deltaTime;
        position.z += velocity.z * deltaTime;

        accumulator += static_cast<double>(position.x) + static_cast<double>(position.y) + static_cast<double>(position.z);
      }
    }
  };

  template <typename Fn>
  BenchmarkResult measure(const std::string &name,
                          std::size_t warmup,
                          std::size_t iterations,
                          double totalOperations,
                          std::string throughputUnit,
                          Fn &&fn)
  {
    using Clock = std::chrono::steady_clock;

    for (std::size_t i = 0; i < warmup; ++i)
    {
      static_cast<void>(fn());
    }

    BenchmarkResult result;
    result.name = name;
    result.throughputUnit = std::move(throughputUnit);
    result.minimumMilliseconds = std::numeric_limits<double>::max();

    std::vector<double> samples;
    samples.reserve(iterations);

    for (std::size_t i = 0; i < iterations; ++i)
    {
      const auto start = Clock::now();
      const double guard = fn();
      const auto end = Clock::now();

      const double milliseconds = std::chrono::duration<double, std::milli>(end - start).count();
      samples.push_back(milliseconds);
      result.guard += guard;
      result.minimumMilliseconds = std::min(result.minimumMilliseconds, milliseconds);
      result.maximumMilliseconds = std::max(result.maximumMilliseconds, milliseconds);
    }

    result.averageMilliseconds = std::accumulate(samples.begin(), samples.end(), 0.0) / static_cast<double>(samples.size());
    const double averageSeconds = result.averageMilliseconds / 1000.0;
    if (averageSeconds > 0.0)
    {
      result.operationsPerSecond = totalOperations / averageSeconds;
    }

    return result;
  }

  BenchmarkConfig parseArguments(int argc, char **argv)
  {
    BenchmarkConfig config;

    for (int index = 1; index < argc; ++index)
    {
      const std::string argument = argv[index];
      const auto requireValue = [&](const std::string &flag) -> std::size_t {
        if (index + 1 >= argc)
        {
          throw std::runtime_error("missing value for " + flag);
        }

        ++index;
        return static_cast<std::size_t>(std::stoull(argv[index]));
      };

      if (argument == "--entities")
      {
        config.entities = requireValue(argument);
      }
      else if (argument == "--frames")
      {
        config.frames = requireValue(argument);
      }
      else if (argument == "--iterations")
      {
        config.iterations = requireValue(argument);
      }
      else if (argument == "--warmup")
      {
        config.warmup = requireValue(argument);
      }
      else if (argument == "--help" || argument == "-h")
      {
        std::cout << "Usage: ecs_benchmark [--entities N] [--frames N] [--iterations N] [--warmup N]\n";
        std::exit(0);
      }
      else
      {
        throw std::runtime_error("unknown argument: " + argument);
      }
    }

    if (config.entities == 0 || config.frames == 0 || config.iterations == 0)
    {
      throw std::runtime_error("entities, frames, and iterations must be greater than zero");
    }

    return config;
  }

  BenchmarkResult benchmarkSpawnEntities(const BenchmarkConfig &config)
  {
    return measure("spawn_entities",
                   config.warmup,
                   config.iterations,
                   static_cast<double>(config.entities),
                   "entities/s",
                   [&]() {
                     hades::EntityManager entityManager;
                     double guard = 0.0;

                     for (std::size_t i = 0; i < config.entities; ++i)
                     {
                       guard += static_cast<double>(entityManager.createEntity());
                     }

                     guard += static_cast<double>(entityManager.getAllEntities().size());
                     return guard;
                   });
  }

  BenchmarkResult benchmarkSpawnAndAttach(const BenchmarkConfig &config)
  {
    return measure("spawn_and_attach_position_velocity",
                   config.warmup,
                   config.iterations,
                   static_cast<double>(config.entities),
                   "entities/s",
                   [&]() {
                     hades::EntityManager entityManager;
                     hades::ComponentManager componentManager;
                     std::bitset<MAX_COMPONENTS> signature;
                     signature.set(0);
                     signature.set(1);
                     double guard = 0.0;

                     for (std::size_t i = 0; i < config.entities; ++i)
                     {
                       const auto entity = entityManager.createEntity();

                       componentManager.addComponent<Position>(
                         entity,
                         Position{static_cast<float>(i), static_cast<float>(i) * 0.5f, static_cast<float>(i) * 0.25f});
                       componentManager.addComponent<Velocity>(entity, Velocity{1.0f, 0.5f, 0.25f});
                       entityManager.setComponentSignature(entity, signature);

                       guard += static_cast<double>(componentManager.getComponent<Position>(entity).x);
                     }

                     guard += static_cast<double>(entityManager.getAllEntities().size());
                     return guard;
                   });
  }

  BenchmarkResult benchmarkSystemUpdate(const BenchmarkConfig &config)
  {
    hades::EntityManager entityManager;
    hades::ComponentManager componentManager;
    hades::SystemManager systemManager;
    auto movementSystem = systemManager.registerSystem<BenchmarkMovementSystem>();

    std::bitset<MAX_COMPONENTS> signature;
    signature.set(0);
    signature.set(1);

    for (std::size_t i = 0; i < config.entities; ++i)
    {
      const auto entity = entityManager.createEntity();

      componentManager.addComponent<Position>(
        entity,
        Position{static_cast<float>(i), static_cast<float>(i) * 0.5f, static_cast<float>(i) * 0.25f});
      componentManager.addComponent<Velocity>(
        entity,
        Velocity{1.0f + static_cast<float>(i % 7) * 0.01f,
                 0.5f + static_cast<float>(i % 11) * 0.01f,
                 0.25f + static_cast<float>(i % 13) * 0.01f});
      entityManager.setComponentSignature(entity, signature);
    }

    auto result = measure("system_update_position_velocity",
                          config.warmup,
                          config.iterations,
                          static_cast<double>(config.entities) * static_cast<double>(config.frames),
                          "entity updates/s",
                          [&]() {
                            movementSystem->accumulator = 0.0;

                            for (std::size_t frame = 0; frame < config.frames; ++frame)
                            {
                              systemManager.updateSystems(1.0f / 60.0f, componentManager, entityManager);
                            }

                            return movementSystem->accumulator;
                          });

    result.millisecondsPerFrame = result.averageMilliseconds / static_cast<double>(config.frames);
    return result;
  }

  BenchmarkResult benchmarkDestroyEntities(const BenchmarkConfig &config)
  {
    return measure("destroy_entities",
                   config.warmup,
                   config.iterations,
                   static_cast<double>(config.entities),
                   "entities/s",
                   [&]() {
                     hades::EntityManager entityManager;
                     std::vector<hades::Entity::EntityId> entities;
                     entities.reserve(config.entities);

                     for (std::size_t i = 0; i < config.entities; ++i)
                     {
                       entities.push_back(entityManager.createEntity());
                     }

                     for (const auto entity : entities)
                     {
                       entityManager.destroyEntity(entity);
                     }

                     return static_cast<double>(entityManager.getAllEntities().size());
                   });
  }

  void printResult(const BenchmarkResult &result)
  {
    std::cout << std::left << std::setw(36) << result.name
              << " avg " << std::right << std::setw(9) << std::fixed << std::setprecision(3) << result.averageMilliseconds
              << " ms";

    if (result.millisecondsPerFrame > 0.0)
    {
      std::cout << "  (" << std::setw(7) << std::fixed << std::setprecision(3) << result.millisecondsPerFrame << " ms/frame)";
    }

    std::cout << "  min " << std::setw(9) << std::fixed << std::setprecision(3) << result.minimumMilliseconds
              << " ms  max " << std::setw(9) << std::fixed << std::setprecision(3) << result.maximumMilliseconds
              << " ms  throughput " << std::setw(12) << std::fixed << std::setprecision(2) << result.operationsPerSecond
              << ' ' << result.throughputUnit << '\n';
  }
}

int main(int argc, char **argv)
{
  try
  {
    const BenchmarkConfig config = parseArguments(argc, argv);

    std::cout << "Hades ECS benchmark\n";
    std::cout << "config: entities=" << config.entities << ", frames=" << config.frames
              << ", iterations=" << config.iterations << ", warmup=" << config.warmup << "\n\n";

    const auto spawnResult = benchmarkSpawnEntities(config);
    const auto spawnAndAttachResult = benchmarkSpawnAndAttach(config);
    const auto systemUpdateResult = benchmarkSystemUpdate(config);
    const auto destroyResult = benchmarkDestroyEntities(config);

    printResult(spawnResult);
    printResult(spawnAndAttachResult);
    printResult(systemUpdateResult);
    printResult(destroyResult);

    const double combinedGuard =
      spawnResult.guard + spawnAndAttachResult.guard + systemUpdateResult.guard + destroyResult.guard;
    std::cout << "\nguard: " << std::fixed << std::setprecision(3) << combinedGuard << '\n';
  }
  catch (const std::exception &exception)
  {
    std::cerr << "ecs_benchmark error: " << exception.what() << '\n';
    return 1;
  }

  return 0;
}
