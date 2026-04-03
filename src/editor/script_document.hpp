#ifndef HADES_EDITOR_SCRIPT_DOCUMENT_HPP
#define HADES_EDITOR_SCRIPT_DOCUMENT_HPP

#include <filesystem>
#include <string>

namespace hades
{
  struct ScriptDocumentSnapshot
  {
    std::string contents;
    std::filesystem::file_time_type lastWriteTime{};
    bool hasLastWriteTime = false;
  };

  bool load_script_document(
      const std::filesystem::path &path,
      ScriptDocumentSnapshot &snapshot,
      std::string *errorMessage = nullptr);

  bool save_script_document(
      const std::filesystem::path &path,
      const std::string &contents,
      ScriptDocumentSnapshot *snapshot = nullptr,
      std::string *errorMessage = nullptr);
}

#endif
