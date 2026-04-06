#ifndef HADES_ENGINE_API_HADES_API_HPP
#define HADES_ENGINE_API_HADES_API_HPP

#ifdef HADES_ENABLE_API

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <condition_variable>

namespace hades
{
  class HadesAPI
  {
  public:
    struct Config
    {
      int port = 7777;
    };

    struct KeyEvent
    {
      int keyCode = 0;
      bool down = true;
    };

    HadesAPI();
    ~HadesAPI();

    HadesAPI(const HadesAPI &) = delete;
    HadesAPI &operator=(const HadesAPI &) = delete;

    bool start(const Config &config);
    void stop();
    bool is_running() const;

    // --- Called by the game loop thread ---

    // Block until the API receives a command (step or reset). Returns false if
    // the server has been stopped (the game loop should exit).
    bool wait_for_command();

    // Check what command is pending after wait_for_command returns.
    bool has_pending_step() const;
    bool has_pending_reset() const;

    // Consume the pending step request and return the number of ticks to advance.
    int consume_pending_step();

    // Consume queued key inputs for the current step.
    std::vector<KeyEvent> consume_pending_inputs();

    // Consume the pending reset request.
    void consume_pending_reset();

    // Update the state returned by the next response.
    void set_observed_state(const std::string &observationsJson);
    void set_entity_state(const std::string &entitiesJson);
    void set_game_over(bool over);

    // Signal that the requested step/reset has completed. This unblocks the
    // HTTP handler so it can return the response to the client.
    void signal_step_complete();
    void signal_reset_complete();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
  };
}

#endif // HADES_ENABLE_API
#endif
