#include "rcrl_parser.h"

#include <clang-c/Index.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "clang-c/CXString.h"
#include "config.h"

using std::cerr;
using std::cout;
using std::endl;
using std::string;

namespace rcrl {
std::ostream& operator<<(std::ostream& stream, const CXString& str) {
  stream << clang_getCString(str);
  clang_disposeString(str);
  return stream;
}
bool operator<(const Point& p1, const Point& p2) {
  return p1.line < p2.line || (p1.line == p2.line && p1.column < p2.column);
}
std::ostream& operator<<(std::ostream& out, const Point& p) {
  return out << p.line << ":" << p.column;
}
std::ostream& operator<<(std::ostream& out, const CodeBlock& p) {
  return out << p.start_pos.line << ":" << p.start_pos.column;
}

auto AstVisitor(CXCursor c, CXCursor parent, CXClientData code_blocks_ptr) {
  // supress compiler warnning
  (void)parent;
  auto return_val = CXChildVisit_Continue;
  auto& code_blocks =
      *reinterpret_cast<std::vector<CodeBlock>*>(code_blocks_ptr);
  clang_getExpansionLocation(clang_getCursorLocation(c), nullptr, nullptr,
                             nullptr, nullptr);
  // parse file when:
  //                  in main file
  if (clang_Location_isFromMainFile(clang_getCursorLocation(c)) != 0) {
    unsigned int lin, col;
    CodeBlock code;
    if ((clang_isDeclaration(clang_getCursorKind(c)) != 0 &&
         clang_isInvalidDeclaration(c) == 0 &&
         clang_isCursorDefinition(c) != 0) ||
        (clang_getCursorKind(c) == CXCursor_InclusionDirective) ||
        (clang_getCursorKind(c) == CXCursor_MacroDefinition) ||
        (clang_getCursorKind(c) == CXCursor_NamespaceAlias)) {
      if (clang_getCursorKind(c) == CXCursor_Namespace) {
        return_val = CXChildVisit_Recurse;
      }
      clang_getExpansionLocation(clang_getRangeStart(clang_getCursorExtent(c)),
                                 nullptr, &lin, &col, nullptr);
      // extend macro definition to include "#define "
      if (clang_getCursorKind(c) == CXCursor_MacroDefinition) {
        col = 1;
      }
      code.start_pos.column = col;
      code.start_pos.line = lin;
      clang_getExpansionLocation(clang_getRangeEnd(clang_getCursorExtent(c)),
                                 nullptr, &lin, &col, nullptr);
      code.end_pos.column = col;
      code.end_pos.line = lin;
      code.cursor = c;

      code_blocks.emplace_back(code);
    }
  }
  return return_val;
}

void GenerateCodeBlocksFromAst(CXTranslationUnit ast,
                               std::vector<CodeBlock>* code_blocks_ptr) {
  CXCursor cursor = clang_getTranslationUnitCursor(ast);
  clang_visitChildren(cursor, AstVisitor, code_blocks_ptr);
}

void PluginParser::Parse() {
  std::ifstream file(file_name_, std::fstream::in);
  string line;
  file_content_.clear();
  while (std::getline(file, line)) {
    file_content_.emplace_back(line + "\n");
  }
  file.close();
  code_blocks_.clear();
  name_space_end_.clear();
  CXIndex index = clang_createIndex(0, 0);
  std::vector<const char*> flags(flags_.size());
  auto i = 0;
  for (const auto& f : flags_) {
    flags[i++] = f.c_str();
  }
  CXTranslationUnit ast = clang_parseTranslationUnit(
      index, file_name_.c_str(), flags.data(), flags.size(), nullptr, 0,
      CXTranslationUnit_DetailedPreprocessingRecord |  // make headers readable
          CXTranslationUnit_Incomplete | CXTranslationUnit_KeepGoing |
          CXTranslationUnit_CreatePreambleOnFirstParse |
          CXTranslationUnit_IgnoreNonErrorsFromIncludedFiles |
          CXTranslationUnit_IncludeAttributedTypes);
  GenerateCodeBlocksFromAst(ast, &code_blocks_);
  ast_ = std::make_tuple(index, ast);
}

void PluginParser::UpdateAstWithOtherFlags() {
  auto [i, tu] = ast_;
  clang_disposeTranslationUnit(tu);
  std::vector<const char*> flags(flags_.size());
  auto ix = 0;
  for (const auto& f : flags_) {
    flags[ix++] = f.c_str();
  }
  CXTranslationUnit ast = clang_parseTranslationUnit(
      i, file_name_.c_str(), flags.data(), flags.size(), nullptr, 0,
      CXTranslationUnit_DetailedPreprocessingRecord |  // make headers readable
          CXTranslationUnit_Incomplete | CXTranslationUnit_KeepGoing |
          CXTranslationUnit_CreatePreambleOnFirstParse |
          CXTranslationUnit_IgnoreNonErrorsFromIncludedFiles |
          CXTranslationUnit_IncludeAttributedTypes);
  ast_ = std::make_tuple(i, ast);
}

void PluginParser::Reparse() {
  std::ifstream file(file_name_, std::fstream::in);
  string line;
  file_content_.clear();
  while (std::getline(file, line)) {
    file_content_.emplace_back(line + "\n");
  }
  file.close();
  name_space_end_.clear();
  code_blocks_.clear();
  auto ast = std::get<1>(ast_);
  clang_reparseTranslationUnit(ast, 0, 0, CXReparse_None);
  GenerateCodeBlocksFromAst(ast, &code_blocks_);
}

PluginParser::PluginParser(string file_name, std::vector<string> flags)
    : generated_file_content_(""),
      flags_(flags),
      file_name_(file_name),
      code_gen_number_(0) {
  // create empty file
  std::ofstream file(file_name_, std::fstream::out | std::fstream::trunc);
  file << "\n";
  file.close();
  Parse();
}

PluginParser::~PluginParser() {
  auto [i, ast] = ast_;
  clang_disposeTranslationUnit(ast);
  clang_disposeIndex(i);
}

string PluginParser::get_file_name() { return file_name_; }
std::vector<string> PluginParser::get_flags() { return flags_; }
void PluginParser::set_flags(std::vector<string> f) {
  flags_ = f;
  UpdateAstWithOtherFlags();
}

string PluginParser::ReadToOneOfCharacters(Point start, string chars) {
  auto i = start.line - 1;
  auto j = start.column - 1;
  auto n = file_content_.size();
  auto result = string("");
  auto line = file_content_[i].substr(j);
  bool not_found = true;
  for (; not_found && i < n; j = 0, i++) {
    if (j == 0) {
      line = file_content_[i];
    } else {
      j = 0;
    }
    do {
      not_found = (chars.find(line[j++]) == chars.npos);
    } while (not_found && j < line.size());
    result += not_found && j > 0 ? line : line.substr(0, j - 1);
  }
  return result;
}
// end point shouldn't be taken
void PluginParser::AppendRange(Point start, Point end) {
  if (start < end) {
    if (start.line == end.line) {
      generated_file_content_.append(file_content_[start.line - 1].substr(
          start.column - 1, end.column - start.column));
    } else {
      while (start.line < end.line) {
        generated_file_content_.append(file_content_[start.line - 1]);
        start.line++;
      }
      generated_file_content_.append(
          file_content_[end.line - 1].substr(0, end.column - 1));
    }
  }
}

void PluginParser::AppendValidCodeBlockWithoutNamespace(CodeBlock code) {
  switch (clang_getCursorKind(code.cursor)) {
    case CXCursor_Namespace: {
      auto str = ReadToOneOfCharacters(code.start_pos, "{") + "{\n";
      name_space_end_.emplace_back(
          std::make_tuple(code.start_pos, code.end_pos, str));
      break;
    }
    default: {
      // global and variable declarations
      AppendRange(code.start_pos, code.end_pos);
      break;
    }
  }
  if (clang_getCursorKind(code.cursor) != CXCursor_InclusionDirective &&
      clang_getCursorKind(code.cursor) != CXCursor_MacroDefinition) {
    generated_file_content_.append(";\n");
  } else {
    generated_file_content_.append("\n");
  }
}

void PluginParser::AppendValidCodeBlock(CodeBlock code) {
  int i = 0;
  for (const auto& [start, end, str] : name_space_end_) {
    if (start < code.start_pos && code.end_pos < end) {
      generated_file_content_.append(str);
      i++;
    }
  }
  AppendValidCodeBlockWithoutNamespace(code);
  for (int j = 0; j < i; ++j) {
    generated_file_content_.append("}\n");
  }
}

void PluginParser::AppendOnceCodeBlocks() {
  std::sort(code_blocks_.begin(), code_blocks_.end(),
            [](const CodeBlock& a, const CodeBlock& b) {
              return a.start_pos < b.start_pos;
            });
  Point p = {1, 1};
  auto line = p.line;
  auto column = p.column;
  // append every unparsed piece of text to once function.
  for (auto c : code_blocks_) {
    while (line < c.start_pos.line) {
      generated_file_content_.append(
          file_content_[line - 1].substr(column - 1));
      line++;
      column = 1;
    }
    generated_file_content_.append(file_content_[line - 1].substr(
        column - 1, c.start_pos.column - column));
    if (clang_getCursorKind(c.cursor) != CXCursor_Namespace) {
      line = c.end_pos.line;
      column = c.end_pos.column;
    } else {
      // skip to '{'
      auto namespace_begin = ReadToOneOfCharacters(c.start_pos, "{") + "{";
      line += std::count(namespace_begin.begin(), namespace_begin.end(), '\n');
      column = file_content_[line].find("{") + 1;
      generated_file_content_.append(namespace_begin + "\n");
      // closing '}' will be appended by the above procedure
    }
  }
  while (line < file_content_.size()) {
    generated_file_content_.append(file_content_[line - 1].substr(column - 1));
    line++;
    column = 1;
  }
  generated_file_content_.append(file_content_[line - 1].substr(column - 1));
}

string PluginParser::get_once_function_name() {
  return string("__rcrl_internal_once_function_" +
                std::to_string(code_gen_number_ - 1));
}

void PluginParser::GenerateSourceFile(string file_name, string prepend_str,
                                      string append_str) {
  std::vector<unsigned int> lines;
  generated_file_content_ = "";
  generated_file_content_ += prepend_str;
  for (const auto& code : code_blocks_) {
    switch (clang_getCursorKind(code.cursor)) {
      case CXCursor_MacroDefinition:
      case CXCursor_Namespace:
      case CXCursor_FunctionTemplate:
      case CXCursor_InclusionDirective:
      case CXCursor_UsingDirective:
      case CXCursor_EnumConstantDecl:
      case CXCursor_TypeAliasTemplateDecl:
      case CXCursor_TypedefDecl:
      case CXCursor_ClassTemplate:
      case CXCursor_ClassTemplatePartialSpecialization:
      case CXCursor_StructDecl:
      case CXCursor_TypeAliasDecl:
      case CXCursor_UnionDecl:
      case CXCursor_UsingDeclaration:
      case CXCursor_EnumDecl:
      case CXCursor_ClassDecl:
      case CXCursor_NamespaceAlias:
      case CXCursor_OverloadedDeclRef: {
        AppendValidCodeBlock(code);
        break;
      }
      default: {
        // variable/functions declarations
        if (clang_getCursorKind(code.cursor) != CXCursor_FunctionDecl &&
            clang_getCursorKind(code.cursor) != CXCursor_VarDecl) {
          assert(false);
        }
        generated_file_content_ += __STR(RCRL_EXPORT_API) + string(" ");
        AppendValidCodeBlock(code);
        break;
      }
    }
  }
  generated_file_content_ +=
      "\n" __STR(RCRL_EXPORT_API) " void __rcrl_internal_once_function_" +
      std::to_string(code_gen_number_) + "(){\n";
  AppendOnceCodeBlocks();
  generated_file_content_ += "}\n" + append_str;
  std::ofstream file(file_name, std::fstream::out | std::fstream::trunc);
  file << generated_file_content_;
  code_gen_number_++;
}

void PluginParser::GenerateHeaderFile(string file_name) {
  generated_file_content_ = "";
  for (const auto& code : code_blocks_) {
    switch (clang_getCursorKind(code.cursor)) {
      case CXCursor_Namespace: {
        break;
      }
      case CXCursor_InclusionDirective: {
        if (file_content_[code.start_pos.line - 1].find(
                "#include \"plugin.h\"") != std::string::npos) {
          break;
        }
      }
      case CXCursor_MacroDefinition:
      case CXCursor_FunctionTemplate:
      case CXCursor_NamespaceAlias:
      case CXCursor_UsingDirective:
      case CXCursor_EnumConstantDecl:
      case CXCursor_TypeAliasTemplateDecl:
      case CXCursor_TypedefDecl:
      case CXCursor_ClassTemplate:
      case CXCursor_ClassTemplatePartialSpecialization:
      case CXCursor_StructDecl:
      case CXCursor_TypeAliasDecl:
      case CXCursor_UnionDecl:
      case CXCursor_UsingDeclaration:
      case CXCursor_EnumDecl:
      case CXCursor_ClassDecl:
      case CXCursor_OverloadedDeclRef: {
        AppendValidCodeBlock(code);
        break;
      }
      default: {
        auto c = code.cursor;
        if (clang_getCursorKind(code.cursor) != CXCursor_FunctionDecl &&
            clang_getCursorKind(c) != CXCursor_VarDecl) {
          assert(false);
        }
        // var/func decl
        generated_file_content_ += RCRL_IMPORT_API " extern " +
                                   ReadToOneOfCharacters(code.start_pos, "={") +
                                   ";\n";
        break;
      }
    }
  }
  std::ofstream file(file_name, std::fstream::out | std::fstream::app);
  file << generated_file_content_;
}

}  // namespace rcrl
