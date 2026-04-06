#ifdef HADES_ENABLE_API

#include "hades_api.hpp"

#include <atomic>
#include <cstdio>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <httplib.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace hades
{
  namespace
  {
    constexpr float DEFAULT_FIXED_DT = 1.0f / 60.0f;
  }

  struct HadesAPI::Impl
  {
    Config config;
    httplib::Server server;
    std::thread serverThread;
    std::atomic<bool> running{false};

    // Synchronization between HTTP handler threads and the game loop thread.
    std::mutex commandMutex;
    std::condition_variable commandCV;

    // Pending command state (protected by commandMutex).
    bool pendingStep = false;
    int pendingTickCount = 1;
    float pendingDeltaTime = DEFAULT_FIXED_DT;
    std::vector<KeyEvent> pendingInputs;
    bool pendingReset = false;
    bool stopRequested = false;

    // Response state (protected by commandMutex).
    std::mutex responseMutex;
    std::condition_variable responseCV;
    bool responseReady = false;

    // Observable state (protected by stateMutex).
    std::mutex stateMutex;
    std::string observationsJson = "{}";
    std::string entitiesJson = "[]";
    bool gameOver = false;

    void setup_routes()
    {
      server.Get("/api/status", [this](const httplib::Request &, httplib::Response &res)
                 {
        json body;
        body["status"] = "running";
        body["engine"] = "Hades";
        body["apiVersion"] = 1;
        res.set_content(body.dump(), "application/json"); });

      server.Get("/api/state", [this](const httplib::Request &, httplib::Response &res)
                 {
        std::lock_guard<std::mutex> lock(stateMutex);
        json body;
        body["observations"] = json::parse(observationsJson, nullptr, false);
        body["entities"] = json::parse(entitiesJson, nullptr, false);
        body["gameOver"] = gameOver;
        res.set_content(body.dump(), "application/json"); });

      server.Post("/api/step", [this](const httplib::Request &req, httplib::Response &res)
                  {
        int ticks = 1;
        float dt = DEFAULT_FIXED_DT;
        std::vector<KeyEvent> inputs;

        if (!req.body.empty())
        {
          auto parsed = json::parse(req.body, nullptr, false);
          if (parsed.is_discarded())
          {
            res.status = 400;
            res.set_content(R"({"error":"Invalid JSON"})", "application/json");
            return;
          }

          if (parsed.contains("ticks") && parsed["ticks"].is_number_integer())
          {
            ticks = parsed["ticks"].get<int>();
            if (ticks < 1) ticks = 1;
            if (ticks > 10000) ticks = 10000;
          }

          if (parsed.contains("dt") && parsed["dt"].is_number())
          {
            dt = parsed["dt"].get<float>();
            if (dt <= 0.0f) dt = DEFAULT_FIXED_DT;
          }

          if (parsed.contains("inputs") && parsed["inputs"].is_array())
          {
            for (const auto &input : parsed["inputs"])
            {
              if (!input.is_object() || !input.contains("key"))
              {
                continue;
              }

              KeyEvent event;
              event.keyCode = input["key"].get<int>();
              event.down = !input.contains("action") || !input["action"].is_string() || input["action"].get<std::string>() != "release";
              inputs.push_back(event);
            }
          }
        }

        // Submit command to the game loop.
        {
          std::lock_guard<std::mutex> rlock(responseMutex);
          responseReady = false;
        }
        {
          std::lock_guard<std::mutex> lock(commandMutex);
          pendingStep = true;
          pendingTickCount = ticks;
          pendingDeltaTime = dt;
          pendingInputs = std::move(inputs);
        }
        commandCV.notify_one();

        // Wait for the game loop to finish processing.
        {
          std::unique_lock<std::mutex> lock(responseMutex);
          responseCV.wait(lock, [this] { return responseReady || !running.load(); });
        }

        // Return the resulting state.
        std::lock_guard<std::mutex> lock(stateMutex);
        json body;
        body["observations"] = json::parse(observationsJson, nullptr, false);
        body["entities"] = json::parse(entitiesJson, nullptr, false);
        body["gameOver"] = gameOver;
        res.set_content(body.dump(), "application/json"); });

      server.Post("/api/input", [this](const httplib::Request &req, httplib::Response &res)
                  {
        if (req.body.empty())
        {
          res.status = 400;
          res.set_content(R"({"error":"Request body required"})", "application/json");
          return;
        }

        auto parsed = json::parse(req.body, nullptr, false);
        if (parsed.is_discarded() || !parsed.contains("inputs") || !parsed["inputs"].is_array())
        {
          res.status = 400;
          res.set_content(R"({"error":"Expected {\"inputs\":[...]}"})", "application/json");
          return;
        }

        std::vector<KeyEvent> inputs;
        for (const auto &input : parsed["inputs"])
        {
          if (!input.is_object() || !input.contains("key"))
          {
            continue;
          }
          KeyEvent event;
          event.keyCode = input["key"].get<int>();
          event.down = !input.contains("action") || input["action"].get<std::string>() != "release";
          inputs.push_back(event);
        }

        {
          std::lock_guard<std::mutex> lock(commandMutex);
          pendingInputs.insert(pendingInputs.end(), inputs.begin(), inputs.end());
        }

        json body;
        body["queued"] = static_cast<int>(inputs.size());
        res.set_content(body.dump(), "application/json"); });

      server.Post("/api/reset", [this](const httplib::Request &, httplib::Response &res)
                  {
        {
          std::lock_guard<std::mutex> rlock(responseMutex);
          responseReady = false;
        }
        {
          std::lock_guard<std::mutex> lock(commandMutex);
          pendingReset = true;
        }
        commandCV.notify_one();

        // Wait for the game loop to finish resetting.
        {
          std::unique_lock<std::mutex> lock(responseMutex);
          responseCV.wait(lock, [this] { return responseReady || !running.load(); });
        }

        std::lock_guard<std::mutex> lock(stateMutex);
        json body;
        body["observations"] = json::parse(observationsJson, nullptr, false);
        body["entities"] = json::parse(entitiesJson, nullptr, false);
        body["gameOver"] = gameOver;
        res.set_content(body.dump(), "application/json"); });
    }
  };

  HadesAPI::HadesAPI() : impl_(std::make_unique<Impl>()) {}

  HadesAPI::~HadesAPI()
  {
    stop();
  }

  bool HadesAPI::start(const Config &config)
  {
    if (impl_->running.load())
    {
      return true;
    }

    impl_->config = config;
    impl_->setup_routes();
    impl_->running.store(true);

    impl_->serverThread = std::thread([this]()
                                      {
      std::fprintf(stderr, "HadesAPI: listening on http://localhost:%d\n", impl_->config.port);
      if (!impl_->server.listen("0.0.0.0", impl_->config.port))
      {
        if (impl_->running.load())
        {
          std::fprintf(stderr, "HadesAPI: failed to bind to port %d\n", impl_->config.port);
        }
      }
      impl_->running.store(false);
      // Wake the game loop if it is blocked waiting for a command.
      impl_->commandCV.notify_all(); });

    return true;
  }

  void HadesAPI::stop()
  {
    if (!impl_->running.load())
    {
      return;
    }

    impl_->running.store(false);
    impl_->server.stop();

    // Wake anything that is waiting.
    {
      std::lock_guard<std::mutex> lock(impl_->commandMutex);
      impl_->stopRequested = true;
    }
    impl_->commandCV.notify_all();
    impl_->responseCV.notify_all();

    if (impl_->serverThread.joinable())
    {
      impl_->serverThread.join();
    }
  }

  bool HadesAPI::is_running() const
  {
    return impl_->running.load();
  }

  bool HadesAPI::wait_for_command()
  {
    std::unique_lock<std::mutex> lock(impl_->commandMutex);
    impl_->commandCV.wait(lock, [this]
                          { return impl_->pendingStep || impl_->pendingReset || impl_->stopRequested || !impl_->running.load(); });
    return impl_->running.load() && !impl_->stopRequested;
  }

  bool HadesAPI::has_pending_step() const
  {
    std::lock_guard<std::mutex> lock(impl_->commandMutex);
    return impl_->pendingStep;
  }

  bool HadesAPI::has_pending_reset() const
  {
    std::lock_guard<std::mutex> lock(impl_->commandMutex);
    return impl_->pendingReset;
  }

  int HadesAPI::consume_pending_step()
  {
    std::lock_guard<std::mutex> lock(impl_->commandMutex);
    impl_->pendingStep = false;
    return impl_->pendingTickCount;
  }

  std::vector<HadesAPI::KeyEvent> HadesAPI::consume_pending_inputs()
  {
    std::lock_guard<std::mutex> lock(impl_->commandMutex);
    std::vector<KeyEvent> result;
    std::swap(result, impl_->pendingInputs);
    return result;
  }

  void HadesAPI::consume_pending_reset()
  {
    std::lock_guard<std::mutex> lock(impl_->commandMutex);
    impl_->pendingReset = false;
  }

  void HadesAPI::set_observed_state(const std::string &observationsJson)
  {
    std::lock_guard<std::mutex> lock(impl_->stateMutex);
    impl_->observationsJson = observationsJson;
  }

  void HadesAPI::set_entity_state(const std::string &entitiesJson)
  {
    std::lock_guard<std::mutex> lock(impl_->stateMutex);
    impl_->entitiesJson = entitiesJson;
  }

  void HadesAPI::set_game_over(bool over)
  {
    std::lock_guard<std::mutex> lock(impl_->stateMutex);
    impl_->gameOver = over;
  }

  void HadesAPI::signal_step_complete()
  {
    {
      std::lock_guard<std::mutex> lock(impl_->responseMutex);
      impl_->responseReady = true;
    }
    impl_->responseCV.notify_all();
  }

  void HadesAPI::signal_reset_complete()
  {
    {
      std::lock_guard<std::mutex> lock(impl_->responseMutex);
      impl_->responseReady = true;
    }
    impl_->responseCV.notify_all();
  }
}

#endif // HADES_ENABLE_API
