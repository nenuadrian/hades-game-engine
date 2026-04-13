#include "script_analysis.hpp"

#include <fstream>
#include <regex>

namespace hades
{
  std::vector<ParsedScriptClass> parse_script_classes(const std::filesystem::path &scriptPath)
  {
    std::vector<ParsedScriptClass> classes;

    std::ifstream file(scriptPath);
    if (!file.is_open())
    {
      return classes;
    }

    const std::regex macroRegex(R"(HADES_REGISTER_SCRIPT\s*\(\s*(\w+)\s*\))");

    std::string line;
    while (std::getline(file, line))
    {
      std::smatch match;
      if (std::regex_search(line, match, macroRegex))
      {
        ParsedScriptClass parsedClass;
        parsedClass.simpleName = match[1].str();
        parsedClass.qualifiedName = parsedClass.simpleName;
        classes.push_back(std::move(parsedClass));
      }
    }

    return classes;
  }

  const ParsedScriptClass *find_script_class(
      const std::vector<ParsedScriptClass> &classes,
      const std::string &className)
  {
    for (const auto &parsedClass : classes)
    {
      if (parsedClass.qualifiedName == className || parsedClass.simpleName == className)
      {
        return &parsedClass;
      }
    }

    return nullptr;
  }
}
