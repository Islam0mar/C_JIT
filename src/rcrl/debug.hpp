/*
 *    \file main.c
 *
 * Copyright (c) 2020 Islam Omar (io1131@fayoum.edu.eg)
 */

#include <bits/stdc++.h>

#include <experimental/type_traits>
#include <type_traits>

template <typename T>
constexpr auto type_name() noexcept {
  std::string_view name = "Error: unsupported compiler", prefix, suffix;
#ifdef __clang__
  name = __PRETTY_FUNCTION__;
  prefix = "auto type_name() [T = ";
  suffix = "]";
#elif defined(__GNUC__)
  name = __PRETTY_FUNCTION__;
  prefix = "constexpr auto type_name() [with T = ";
  suffix = "]";
#elif defined(_MSC_VER)
  name = __FUNCSIG__;
  prefix = "auto __cdecl type_name<";
  suffix = ">(void) noexcept";
#endif
  name.remove_prefix(prefix.size());
  name.remove_suffix(suffix.size());
  return name;
}

using std::cerr;
using std::cout;
using std::endl;
using std::string;
// template <typename T>
// using std::vector<T>;
// template <typename T>
// using std::pair<T>;
template <typename T>
void __print(T x);
void __print(std::int32_t x) { cerr << x; }
void __print(std::uint32_t x) { cerr << x; }
void __print(std::int16_t x) { cerr << x; }
void __print(std::uint16_t x) { cerr << x; }
void __print(std::int8_t x) { cerr << static_cast<std::int32_t>(x); }
void __print(std::uint8_t x) { cerr << static_cast<std::uint32_t>(x); }
void __print(std::int64_t x) { cerr << x; }
void __print(std::uint64_t x) { cerr << x; }
void __print(float x) { cerr << x; }
void __print(double x) { cerr << x; }
void __print(long double x) { cerr << x; }
template <typename T>
void __print(std::complex<T> x) {
  cerr << '{';
  __print(x.real());
  cerr << ',';
  __print(x.imag());
  cerr << '}';
}
void __print(char x) { cerr << '\'' << x << '\''; }
void __print(char* x) { cerr << '\"' << x << '\"'; }
void __print(string x) { cerr << '\"' << x << '\"'; }
void __print(bool x) { cerr << (x ? "true" : "false"); }
template <typename T, typename V>
void __print(std::pair<T, V> x) {
  cerr << '{';
  __print(x.first);
  cerr << ',';
  __print(x.second);
  cerr << '}';
}
template <typename T>
void __print(T x) {
  auto f = 0U;
  cerr << '{';
  for (auto& i : x) cerr << (f++ ? "," : ""), __print(i);
  cerr << "}";
}
template <typename T, size_t N>
void __print(T const (&x)[N]) {
  cerr << '{';
  for (auto i = N - N; i < N;) cerr << (i++ ? "," : ""), __print(x[i]);
  cerr << "}";
}
template <typename T>
void __print(const T* x) {
  auto w = const_cast<T*>(x);
  __print(w);
}
template <typename T, bool = true>
struct Print {
  void operator()(T x) { __print(x); }
};
template <typename T>
struct Print<T, false> {
  void operator()(T x) { cerr << x; }
};
template <typename T>
using print_function_t = decltype(__print(std::declval<T>()));
void _print() { cerr << "]\n"; }
template <typename T, typename... V>
void _print(T x, V... v) {
  Print<T, std::experimental::is_detected_v<print_function_t, T>>()(x);
  if (sizeof...(v)) cerr << ", ";
  _print(v...);
}
#ifndef DEBUG_MODE
#define DEBUG(x...)             \
  cerr << "[" << #x << "] = ["; \
  _print(x)
#else
#define DEBUG(x...)
#endif
