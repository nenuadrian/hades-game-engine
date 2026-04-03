#include "script_document.hpp"

#include <fstream>
#include <iterator>
#include <sstream>
#include <utility>

namespace hades
{
  namespace
  {
    void clear_snapshot(ScriptDocumentSnapshot &snapshot)
    {
      snapshot.contents.clear();
      snapshot.lastWriteTime = std::filesystem::file_time_type{};
      snapshot.hasLastWriteTime = false;
    }

    void set_error_message(std::string *errorMessage, const std::string &message)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = message;
      }
    }
  }

  bool load_script_document(
      const std::filesystem::path &path,
      ScriptDocumentSnapshot &snapshot,
      std::string *errorMessage)
  {
    clear_snapshot(snapshot);

    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
      set_error_message(errorMessage, "Unable to open script '" + path.string() + "'.");
      return false;
    }

    snapshot.contents.assign(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());

    if (!input.good() && !input.eof())
    {
      clear_snapshot(snapshot);
      set_error_message(errorMessage, "Unable to read script '" + path.string() + "'.");
      return false;
    }

    std::error_code errorCode;
    snapshot.lastWriteTime = std::filesystem::last_write_time(path, errorCode);
    snapshot.hasLastWriteTime = !errorCode;
    return true;
  }

  bool save_script_document(
      const std::filesystem::path &path,
      const std::string &contents,
      ScriptDocumentSnapshot *snapshot,
      std::string *errorMessage)
  {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
    {
      set_error_message(errorMessage, "Unable to save script '" + path.string() + "'.");
      return false;
    }

    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!output.good())
    {
      set_error_message(errorMessage, "Unable to write script '" + path.string() + "'.");
      return false;
    }

    output.close();
    if (!output)
    {
      set_error_message(errorMessage, "Unable to finalize script '" + path.string() + "'.");
      return false;
    }

    if (snapshot != nullptr)
    {
      snapshot->contents = contents;
      std::error_code errorCode;
      snapshot->lastWriteTime = std::filesystem::last_write_time(path, errorCode);
      snapshot->hasLastWriteTime = !errorCode;
    }

    return true;
  }
}
