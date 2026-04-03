#include "script_analysis.hpp"

#include <fstream>
#include <optional>
#include <sstream>

namespace hades
{
  namespace
  {
    struct NamespaceScope
    {
      std::string name;
      int bodyDepth = 0;
    };

    struct PendingClassDeclaration
    {
      std::string header;
      std::string namespaceName;
    };

    std::string trim(const std::string &value)
    {
      const auto start = value.find_first_not_of(" \t\r\n");
      if (start == std::string::npos)
      {
        return {};
      }

      const auto end = value.find_last_not_of(" \t\r\n");
      return value.substr(start, end - start + 1);
    }

    std::string strip_line_comment(const std::string &line)
    {
      const auto commentPos = line.find("//");
      if (commentPos == std::string::npos)
      {
        return line;
      }

      return line.substr(0, commentPos);
    }

    std::string current_namespace_name(
        const std::string &fileScopedNamespace,
        const std::vector<NamespaceScope> &namespaceStack)
    {
      std::string result = fileScopedNamespace;
      for (const auto &scope : namespaceStack)
      {
        if (scope.name.empty())
        {
          continue;
        }

        if (!result.empty())
        {
          result += '.';
        }
        result += scope.name;
      }

      return result;
    }

    bool try_parse_namespace_declaration(
        const std::string &trimmedLine,
        std::string &name,
        bool &fileScoped,
        bool &opensBlock)
    {
      if (trimmedLine.rfind("namespace ", 0) != 0)
      {
        return false;
      }

      std::string remainder = trim(trimmedLine.substr(std::string("namespace ").size()));
      fileScoped = false;
      opensBlock = false;

      if (remainder.empty())
      {
        return false;
      }

      if (remainder.back() == ';')
      {
        fileScoped = true;
        remainder.pop_back();
      }
      else if (remainder.back() == '{')
      {
        opensBlock = true;
        remainder.pop_back();
      }

      name = trim(remainder);
      return !name.empty();
    }

    bool contains_hades_script_base(const std::string &baseList)
    {
      std::stringstream stream(baseList);
      std::string baseType;
      while (std::getline(stream, baseType, ','))
      {
        baseType = trim(baseType);
        if (baseType.rfind("global::", 0) == 0)
        {
          baseType = baseType.substr(std::string("global::").size());
        }

        if (baseType == "HadesScript" || baseType == "Hades.Scripting.HadesScript")
        {
          return true;
        }
      }

      return false;
    }

    std::optional<ParsedScriptClass> parse_class_declaration(
        const std::string &header,
        const std::string &namespaceName)
    {
      const auto classPos = header.find("class ");
      if (classPos == std::string::npos)
      {
        return std::nullopt;
      }

      std::size_t cursor = classPos + std::string("class ").size();
      while (cursor < header.size() &&
             (header[cursor] == ' ' || header[cursor] == '\t' || header[cursor] == '\r' || header[cursor] == '\n'))
      {
        ++cursor;
      }

      const std::size_t nameStart = cursor;
      while (cursor < header.size() &&
             ((header[cursor] >= 'A' && header[cursor] <= 'Z') ||
              (header[cursor] >= 'a' && header[cursor] <= 'z') ||
              (header[cursor] >= '0' && header[cursor] <= '9') ||
              header[cursor] == '_'))
      {
        ++cursor;
      }

      if (cursor == nameStart)
      {
        return std::nullopt;
      }

      ParsedScriptClass parsedClass;
      parsedClass.simpleName = header.substr(nameStart, cursor - nameStart);

      const auto headerEnd = header.find('{', cursor);
      const auto colonPos = header.find(':', cursor);
      if (colonPos == std::string::npos || (headerEnd != std::string::npos && colonPos > headerEnd))
      {
        return std::nullopt;
      }

      const auto baseListEnd = headerEnd == std::string::npos ? header.size() : headerEnd;
      const std::string baseList = trim(header.substr(colonPos + 1, baseListEnd - colonPos - 1));
      if (!contains_hades_script_base(baseList))
      {
        return std::nullopt;
      }

      parsedClass.qualifiedName = namespaceName.empty()
                                      ? parsedClass.simpleName
                                      : namespaceName + "." + parsedClass.simpleName;
      return parsedClass;
    }

    bool try_parse_public_field(
        const std::string &trimmedLine,
        std::pair<std::string, std::string> &field)
    {
      if (trimmedLine.find("public") != 0)
      {
        return false;
      }
      if (trimmedLine.find("override") != std::string::npos ||
          trimmedLine.find("virtual") != std::string::npos ||
          trimmedLine.find("static") != std::string::npos ||
          trimmedLine.find("(") != std::string::npos ||
          trimmedLine.find("void") != std::string::npos ||
          trimmedLine.find("class ") != std::string::npos)
      {
        return false;
      }

      std::istringstream iss(trimmedLine);
      std::string keyword;
      std::string type;
      std::string name;
      if (!(iss >> keyword >> type >> name))
      {
        return false;
      }

      while (!name.empty() && (name.back() == ';' || name.back() == '='))
      {
        name.pop_back();
      }

      if (name.empty() || type.empty())
      {
        return false;
      }

      field = {type, name};
      return true;
    }

    int count_char(const std::string &value, char target)
    {
      int count = 0;
      for (const char ch : value)
      {
        if (ch == target)
        {
          ++count;
        }
      }

      return count;
    }
  }

  std::vector<ParsedScriptClass> parse_script_classes(const std::filesystem::path &scriptPath)
  {
    std::vector<ParsedScriptClass> classes;

    std::ifstream file(scriptPath);
    if (!file.is_open())
    {
      return classes;
    }

    int braceDepth = 0;
    std::string fileScopedNamespace;
    std::optional<std::string> pendingNamespace;
    std::vector<NamespaceScope> namespaceStack;
    std::optional<PendingClassDeclaration> pendingClass;
    std::vector<std::pair<std::size_t, int>> activeClassStack;

    std::string line;
    while (std::getline(file, line))
    {
      const std::string lineWithoutComments = strip_line_comment(line);
      const std::string trimmedLine = trim(lineWithoutComments);

      if (pendingNamespace.has_value() && lineWithoutComments.find('{') != std::string::npos)
      {
        namespaceStack.push_back(NamespaceScope{*pendingNamespace, braceDepth + 1});
        pendingNamespace.reset();
      }

      if (!trimmedLine.empty())
      {
        std::string namespaceName;
        bool fileScoped = false;
        bool opensBlock = false;
        if (try_parse_namespace_declaration(trimmedLine, namespaceName, fileScoped, opensBlock))
        {
          if (fileScoped)
          {
            fileScopedNamespace = namespaceName;
          }
          else if (opensBlock)
          {
            namespaceStack.push_back(NamespaceScope{namespaceName, braceDepth + 1});
          }
          else
          {
            pendingNamespace = namespaceName;
          }
        }
        else if (pendingClass.has_value())
        {
          pendingClass->header += ' ';
          pendingClass->header += trimmedLine;
        }
        else if (trimmedLine.find("class ") != std::string::npos)
        {
          pendingClass = PendingClassDeclaration{
              trimmedLine,
              current_namespace_name(fileScopedNamespace, namespaceStack)};
        }
      }

      if (pendingClass.has_value() && pendingClass->header.find('{') != std::string::npos)
      {
        auto parsedClass = parse_class_declaration(pendingClass->header, pendingClass->namespaceName);
        if (parsedClass.has_value())
        {
          classes.push_back(std::move(*parsedClass));
          activeClassStack.emplace_back(classes.size() - 1, braceDepth + 1);
        }

        pendingClass.reset();
      }

      if (!activeClassStack.empty() && braceDepth == activeClassStack.back().second)
      {
        std::pair<std::string, std::string> field;
        if (try_parse_public_field(trimmedLine, field))
        {
          classes[activeClassStack.back().first].publicFields.push_back(std::move(field));
        }
      }

      braceDepth += count_char(lineWithoutComments, '{');
      braceDepth -= count_char(lineWithoutComments, '}');

      while (!activeClassStack.empty() && braceDepth < activeClassStack.back().second)
      {
        activeClassStack.pop_back();
      }

      while (!namespaceStack.empty() && braceDepth < namespaceStack.back().bodyDepth)
      {
        namespaceStack.pop_back();
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
