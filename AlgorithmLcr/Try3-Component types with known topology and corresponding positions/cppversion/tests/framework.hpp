#pragma once
// Minimal test framework (same shape as the Try2 cppversion one).
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
namespace testframework {
struct Case { std::string name; std::function<void()> fn; };
inline std::vector<Case>& registry() { static std::vector<Case> reg; return reg; }
inline int& failures() { static int f = 0; return f; }
inline int& checks() { static int c = 0; return c; }
inline bool& currentFailed() { static bool b = false; return b; }
struct Registrar { Registrar(const std::string& n, std::function<void()> fn) { registry().push_back(Case{n, std::move(fn)}); } };
}
#define TEST(name)     static void test_fn_##name();     static testframework::Registrar reg_##name(#name, test_fn_##name);     static void test_fn_##name()
#define CHECK(cond)     do { ++testframework::checks(); if (!(cond)) { ++testframework::failures(); testframework::currentFailed() = true; std::printf("  FAIL %s:%d CHECK(%s)\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_NEAR(a, b, tol)     do { ++testframework::checks(); double va_ = (a), vb_ = (b); if (!(std::fabs(va_ - vb_) <= (tol) * std::max(1.0, std::fabs(vb_)))) { ++testframework::failures(); testframework::currentFailed() = true; std::printf("  FAIL %s:%d CHECK_NEAR(%g, %g, %g)\n", __FILE__, __LINE__, va_, vb_, (double)(tol)); } } while (0)
#define CHECK_CNEAR(a, b, tol)     do { ++testframework::checks(); auto va_ = (a), vb_ = (b); double d_ = std::abs(va_ - vb_); if (!(d_ <= (tol) * std::max(1.0, std::abs(vb_)))) { ++testframework::failures(); testframework::currentFailed() = true; std::printf("  FAIL %s:%d CHECK_CNEAR(%s,%s) |diff|=%g\n", __FILE__, __LINE__, #a, #b, d_); } } while (0)
