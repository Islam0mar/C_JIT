#include <readline/history.h>
#include <readline/readline.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

#include "rcrl/config.h"
#include "rcrl/rcrl.h"

using std::string;

// TODO: add more vocabulary, maybe add libclang complete!
static std::vector<std::string> vocabulary{"printf"};

// This function is called with state=0 the first time; subsequent calls are
// with a nonzero state. state=0 can be used to perform one-time
// initialization for this completion session.
extern "C" char *my_generator(const char *text, int state) {
  static std::vector<std::string> matches;
  static size_t match_index = 0;

  if (state == 0) {
    // During initialization, compute the actual matches for 'text' and keep
    // them in a static vector.
    matches.clear();
    match_index = 0;

    // Collect a vector of matches: vocabulary words that begin with text.
    std::string textstr = std::string(text);
    for (auto word : vocabulary) {
      if (word.size() >= textstr.size() &&
          word.compare(0, textstr.size(), textstr) == 0) {
        matches.push_back(word);
      }
    }
  }

  if (match_index >= matches.size()) {
    // We return nullptr to notify the caller no more matches are available.
    return nullptr;
  } else {
    // Return a malloc'd char* for the match. The caller frees it.
    return strdup(matches[match_index++].c_str());
  }
}

// Custom completion function
extern "C" char **my_completion(const char *text, int start, int end) {
  rl_completer_quote_characters = "\"'";
  // Don't do filename completion even if our generator finds no matches.
  rl_attempted_completion_over = 1;

  // Note: returning nullptr here will make readline use the default filename
  // completer.
  return rl_completion_matches(text, my_generator);
}

int main() {
  string line, tmp_line;
  bool done = false;

  // enable auto-complete
  rl_bind_key('\t', rl_complete);
  rl_attempted_completion_function = my_completion;

  std::vector<string> args = {"-std=c99", "-O0", "-Wall", "-Wextra", "-ggdb3"};
  rcrl::Plugin compiler("plugin", args);
  int c_open, b_open, c_close, b_close;
  unsigned int i;
  std::cout << " C JIT V" << VERSION_STRING << "\n";
  while (!done) {
    i = 0;
    c_open = b_open = c_close = b_close = 0;
    line = string();
    do {
      line += tmp_line = (0 == i++) ? readline(">> ") : readline("?> ");
      c_open += std::count(tmp_line.begin(), tmp_line.end(), '{');
      b_open += std::count(tmp_line.begin(), tmp_line.end(), '(');
      c_close += std::count(tmp_line.begin(), tmp_line.end(), '}');
      b_close += std::count(tmp_line.begin(), tmp_line.end(), ')');
      if (!line.empty()) {
        line += "\n";
      }
    } while (c_open != c_close || b_open != b_close);
    if (line.compare(".rawInput\n") == 0) {
      do {
        line += tmp_line = readline("?> ");
        line += "\n";
      } while (tmp_line.compare(".rawInput\n") != 0);
    }

    if (line.compare(".q\n") == 0) {
      done = true;
      compiler.CleanupPlugins();
    } else if (line.compare(".clean\n") == 0) {
      compiler.CleanupPlugins();
    } else if (line.substr(0, 6).compare(".flags") == 0) {
      line = line.substr(6);
      size_t start;
      size_t end = 0;
      args.clear();
      while ((start = line.find_first_not_of(' ', end)) != std::string::npos) {
        end = line.find(' ', start);
        args.emplace_back(line.substr(start, end - start));
      }
      compiler.set_flags(args);
    } else if (line.substr(0, 2).compare(".L") == 0) {
      line = line.substr(2);
      compiler.LoadNewLibrary(line.substr(0, line.size() - 1).c_str());
    } else if (line.substr(0, 2).compare(".f") == 0) {
      line = line.substr(2);
      size_t start;
      size_t end = 0;
      args.clear();
      while ((start = line.find_first_not_of(' ', end)) != std::string::npos) {
        end = line.find(' ', start);
        args.emplace_back(line.substr(start, end - start));
      }
      compiler.set_flags(args);
    } else if (!line.empty()) {
      compiler.CompileCode(line);
      int last_compiler_exitcode;
      while (!compiler.TryGetExitStatusFromCompile(last_compiler_exitcode)) {
        std::cout << compiler.get_new_compiler_output();
      }
      // drop signal line
      compiler.get_new_compiler_output();

      // append to the history
      add_history(line.substr(0, line.size() - 1).c_str());

      if (last_compiler_exitcode == 0) {
        // load the new plugin
        compiler.CopyAndLoadNewPlugin();
      }
    }
  }

  return 0;
}
