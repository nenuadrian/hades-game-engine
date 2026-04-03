#ifndef HADES_EDITOR_SCRIPT_ANALYSIS_HPP
#define HADES_EDITOR_SCRIPT_ANALYSIS_HPP

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace hades
{
  struct ParsedScriptClass
  {
    std::string qualifiedName;
    std::string simpleName;
    std::vector<std::pair<std::string, std::string>> publicFields;
  };

  std::vector<ParsedScriptClass> parse_script_classes(const std::filesystem::path &scriptPath);
  const ParsedScriptClass *find_script_class(
      const std::vector<ParsedScriptClass> &classes,
      const std::string &className);
}

#endif
