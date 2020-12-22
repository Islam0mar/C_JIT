#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "rcrl.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "config.h"
#include "rcrl_parser.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef HMODULE RCRL_Dynlib;
#define RDRL_LoadDynlib(lib) LoadLibrary(lib)
#define RCRL_CloseDynlib FreeLibrary
#define RCRL_CopyDynlib(src, dst) CopyFile(src, dst, false)
#define RCRL_System_Delete "del /Q "

#else

#include <dlfcn.h>
typedef void* RCRL_Dynlib;
#define RDRL_LoadDynlib(lib) dlopen(lib, RTLD_LAZY | RTLD_GLOBAL)
#define RDRL_FIND_FUNCTION_IN_DYNAMIC_LIB(handle, name) \
  ((void (*)())dlsym(handle, name))
#define RCRL_CloseDynlib dlclose
#define RCRL_CopyDynlib(src, dst) \
  (!system((string("cp ") + src + " " + dst).c_str()))
#define RCRL_System_Delete "rm "

#endif

namespace bp = boost::process;
using std::cerr;
using std::cout;
using std::endl;
using std::string;

namespace rcrl {
Plugin::Plugin(string file_base_name, std::vector<string> flags)
    : is_compiling_(false), parser_(file_base_name + ".c", flags) {
  auto header = GetHeaderNameFromSourceName(parser_.get_file_name());
  std::ofstream f(header, std::fstream::trunc | std::fstream::out);
  f << "#pragma once\n";
  f.close();
}

void Plugin::set_flags(const std::vector<string>& new_flags) {
  // avoid calling it's constructor at the function end
  static std::future<bool> p;
  assert(!IsCompiling());
  is_compiling_ = true;
  p = std::async(std::launch::async, [&]() {
    parser_.set_flags(new_flags);
    return (is_compiling_ = false);
  });
}

string Plugin::get_new_compiler_output() {
  std::lock_guard<std::mutex> lock(compiler_output_mut_);
  auto str = compiler_output_;
  compiler_output_.clear();
  return str;
}

string Plugin::CleanupPlugins(bool redirect_stdout) {
  assert(!IsCompiling());

  if (redirect_stdout)
    freopen(RCRL_BUILD_FOLDER "/rcrl_stdout.txt", "w", stdout);

  // close the plugins_ in reverse order
  for (auto it = plugins_.rbegin(); it != plugins_.rend(); ++it)
    RCRL_CloseDynlib(it->second);

  string out;

  if (redirect_stdout) {
    fclose(stdout);
    freopen("CON", "w", stdout);

    FILE* f = fopen(RCRL_BUILD_FOLDER "/rcrl_stdout.txt", "rb");
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    out.resize(fsize);
    fread((void*)out.data(), fsize, 1, f);
    fclose(f);
  }

  string bin_folder(RCRL_BIN_FOLDER);
#ifdef _WIN32
  // replace forward slash with windows style slash
  replace(bin_folder.begin(), bin_folder.end(), '/', '\\');
#endif  // _WIN32

  if (plugins_.size())
    system((string(RCRL_System_Delete) + bin_folder +
            RCRL_PLUGIN_NAME "_*" RCRL_EXTENSION)
               .c_str());
  plugins_.clear();

  // reset header file
  auto header = GetHeaderNameFromSourceName(parser_.get_file_name());
  std::ofstream f(header, std::fstream::trunc | std::fstream::out);
  f << "#pragma once\n";
  f.close();

  return out;
}

bool Plugin::CompileCode(string code) {
  assert(!IsCompiling());
  assert(code.size());

  // fix line endings
  replace(code.begin(), code.end(), '\r', '\n');

  // figure out the sections
  std::fstream file(parser_.get_file_name(),
                    std::fstream::in | std::fstream::out | std::fstream::trunc);
  // add header to correctly parse the input
  auto header = GetHeaderNameFromSourceName(parser_.get_file_name());
  file << "#include \"" + header + "\"\n" << code;
  file.close();
  // mark the successful compilation flag as false
  last_compile_successful_ = false;
  compiler_output_.clear();
  is_compiling_ = true;
  // TODO: add buffer size to config file
  static std::vector<char> buf(128);
  compiler_process_ = std::async(std::launch::async, [&]() {
    // reparsing takes some time so moved inside async
    parser_.Reparse();
    parser_.GenerateSourceFile(parser_.get_file_name());
    boost::asio::io_service ios;
    bp::async_pipe ap(ios);
    auto output_buffer = boost::asio::buffer(buf);
    // must use clang++ as g++ differ from libclang deduced types
#ifdef VERY_FAST // FIXME: doesn't compile all interesting apps
    auto cmd = bp::search_path("tcc").string() + string(" ");
#else
    auto cmd = bp::search_path("gcc").string() + string(" ");
#endif
    for (auto flag : parser_.get_flags()) {
      cmd += flag + string(" ");
    }
    cmd += "-shared -fvisibility=hidden -fPIC " + parser_.get_file_name() +
           " -o " + GetBaseNameFromSourceName((parser_.get_file_name())) +
           ".so";
    bp::child c(cmd, (bp::std_err & bp::std_out) > ap, bp::std_in.close());
    auto OnStdout = [&](const boost::system::error_code& ec, std::size_t size) {
      auto lambda_impl = [&](const boost::system::error_code& ec, std::size_t n,
                             auto& lambda_ref) {
        std::lock_guard<std::mutex> lock(compiler_output_mut_);
        compiler_output_.reserve(compiler_output_.size() + n);
        compiler_output_.insert(compiler_output_.end(), buf.begin(),
                                buf.begin() + n);
        if (!ec && ap.is_open()) {
          ap.async_read_some(output_buffer,
                             std::bind(lambda_ref, std::placeholders::_1,
                                       std::placeholders::_2, lambda_ref));
        }
      };
      return lambda_impl(ec, size, lambda_impl);
    };
    ap.async_read_some(output_buffer, OnStdout);
    ios.run();
    c.join();
    is_compiling_ = false;
    return c.exit_code();
  });
  return true;
}

bool Plugin::IsCompiling() { return is_compiling_; }

bool Plugin::TryGetExitStatusFromCompile(int& exit_code) {
  if (compiler_process_.valid() && !IsCompiling()) {
    exit_code = compiler_process_.get();
    last_compile_successful_ = (exit_code == 0);
    compiler_output_.append("\nSignaled with sginal #" +
                            std::to_string(exit_code) + ", aka " +
                            strsignal(exit_code) + "\n");
    if (last_compile_successful_) {
      auto header = GetHeaderNameFromSourceName(parser_.get_file_name());
      parser_.GenerateHeaderFile(header);
    }
    return true;
  }
  return false;
}
string Plugin::CopyAndLoadNewPlugin(bool redirect_stdout) {
  assert(!IsCompiling());
  assert(last_compile_successful_);
  is_compiling_ = true;
  last_compile_successful_ =
      false;  // shouldn't call this function twice in a
              // row without compiling anything in between

  // copy the plugin
  auto name_copied = string(RCRL_BIN_FOLDER) + RCRL_PLUGIN_NAME "_" +
                     std::to_string(plugins_.size()) + RCRL_EXTENSION;
  const auto copy_res = RCRL_CopyDynlib(
      (string(RCRL_BIN_FOLDER) + RCRL_PLUGIN_NAME RCRL_EXTENSION).c_str(),
      name_copied.c_str());
  assert(copy_res);
  if (redirect_stdout)
    freopen(RCRL_BUILD_FOLDER "/rcrl_stdout.txt", "w", stdout);

  // load the plugin
  void* plugin = RDRL_LoadDynlib(name_copied.c_str());
  if (!plugin) {
    fprintf(stderr, "%s\n", dlerror());
    exit(EXIT_FAILURE);
  }
  assert(plugin);

  // load once function
  void (*pf)() = RDRL_FIND_FUNCTION_IN_DYNAMIC_LIB(
      plugin, parser_.get_once_function_name().c_str());
  (*pf)();

  // add the plugin to the list of loaded ones - for later unloading
  plugins_.push_back({name_copied, plugin});

  string out;

  if (redirect_stdout) {
    fclose(stdout);
    freopen("CON", "w", stdout);

    FILE* f = fopen(RCRL_BUILD_FOLDER "/rcrl_stdout.txt", "rb");
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    out.resize(fsize);
    fread((void*)out.data(), fsize, 1, f);
    fclose(f);
  }
  is_compiling_ = false;
  return out;
}

bool Plugin::LoadNewLibrary(string name) {
  name = "lib" + name + ".so";
  void* plugin = RDRL_LoadDynlib(name.c_str());
  if (!plugin) {
    fprintf(stderr, "%s\n", dlerror());
    exit(EXIT_FAILURE);
  }

  // add the plugin to the list of loaded ones - for later unloading
  plugins_.push_back({name, plugin});

  assert(plugin);
  return true;
}

}  // namespace rcrl
