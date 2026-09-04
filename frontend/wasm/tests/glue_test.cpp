// glue_test.cpp — native (host g++) sanity tests for the wasm glue layer.
// Exercises the same C ABI that the browser worker uses, on synthetic
// noiseless data with known ground truth:
//   * try1: series R+L+C one-port -> best candidate wRMSE ~ 0
//   * try2: same circuit as a known multiset (R, C, L+DCR) -> exact recovery
//   * try3: ladder topology (0-L-2, 2-C-1, 2-R-1) -> parameter recovery
//   * error paths: bad measurements, port-open topology, resistor-only set
//
// JSON is checked structurally (key presence + numeric extraction) — full
// schema validation happens on the TS side (frontend unit tests).

#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using C = std::complex<double>;

extern "C" {
const char* lcr_version();
char* lcr_try1(const double*, const double*, const double*, int, int, int, int);
char* lcr_try2(const double*, const double*, const double*, int, const char*,
               const double*, const double*, const int*, int, int);
char* lcr_try3(const double*, const double*, const double*, int, const int*,
               const int*, const char*, int);
void lcr_free(char*);
}

static int g_fail = 0;
#define CHECK(cond, msg)                                              \
    do {                                                              \
        if (cond) {                                                   \
            std::printf("  ok   %s\n", msg);                          \
        } else {                                                      \
            ++g_fail;                                                 \
            std::printf("  FAIL %s\n", msg);                          \
        }                                                             \
    } while (0)

// extract the i-th occurrence of "\"key\":" followed by a number
static bool jsonNum(const char* json, const char* key, int occurrence, double* out) {
    std::string needle = std::string("\"") + key + "\":";
    const char* p = json;
    for (int i = 0; i <= occurrence; ++i) {
        p = std::strstr(p, needle.c_str());
        if (!p) return false;
        p += needle.size();
        if (i < occurrence) continue;
    }
    char* end = nullptr;
    *out = std::strtod(p, &end);
    return end != p;
}

static bool hasStr(const char* json, const char* sub) {
    return std::strstr(json, sub) != nullptr;
}

static std::vector<double> logspace(double lo, double hi, int n) {
    std::vector<double> v(n);
    for (int i = 0; i < n; ++i)
        v[i] = std::pow(10.0, lo + (hi - lo) * i / (n - 1));
    return v;
}

int main() {
    std::printf("version: %s\n", lcr_version());

    // ground truth: R 50Ω + L 1mH(dcr 2Ω) + C 1µF in series, 100 Hz .. 1 MHz
    const double R = 50.0, L = 1e-3, DCR = 2.0, Cc = 1e-6;
    std::vector<double> f = logspace(2.0, 6.0, 40);
    const int n = (int)f.size();
    std::vector<double> re(n), im(n);
    for (int i = 0; i < n; ++i) {
        C s(0.0, 2.0 * M_PI * f[i]);
        C z = R + (DCR + s * L) + 1.0 / (s * Cc);
        re[i] = z.real();
        im[i] = z.imag();
    }

    // ---------------- try1 ----------------
    {
        char* out = lcr_try1(f.data(), re.data(), im.data(), n, 0, 0, 5);
        double wrmse = -1, aicc = 0;
        CHECK(hasStr(out, "\"ok\":true"), "try1 ok");
        CHECK(jsonNum(out, "wrmse", 0, &wrmse) && wrmse < 1e-6, "try1 best wRMSE ~ 0");
        CHECK(jsonNum(out, "aicc", 0, &aicc), "try1 aicc present");
        CHECK(hasStr(out, "\"adjacency\""), "try1 adjacency present");
        CHECK(hasStr(out, "\"theory\""), "try1 theory present");
        CHECK(hasStr(out, "\"rank\":1"), "try1 rank 1 present");
        lcr_free(out);
    }
    // exact-n constraint = 3 devices
    {
        char* out = lcr_try1(f.data(), re.data(), im.data(), n, 3, 0, 5);
        double devices = 0, wrmse = -1;
        CHECK(hasStr(out, "\"ok\":true"), "try1 exact-3 ok");
        CHECK(jsonNum(out, "devices", 0, &devices) && devices == 3, "try1 exact-3 devices == 3");
        CHECK(jsonNum(out, "wrmse", 0, &wrmse) && wrmse < 1e-4, "try1 exact-3 recovers circuit");
        lcr_free(out);
    }
    // bad input
    {
        std::vector<double> bf{1, -2, 3, 4}, bre{1, 2, 3, 4}, bim{1, 2, 3, 4};
        char* out = lcr_try1(bf.data(), bre.data(), bim.data(), 4, 0, 0, 5);
        CHECK(hasStr(out, "\"ok\":false") && hasStr(out, "bad_input"), "try1 rejects f <= 0");
        lcr_free(out);
    }

    // ---------------- try2 ----------------
    {
        const char kinds[3] = {'R', 'C', 'L'};
        const double vals[3] = {50.0, 1e-6, 1e-3};
        const double dcrs[3] = {0.0, 0.0, 2.0};
        const int counts[3] = {1, 1, 1};
        char* out = lcr_try2(f.data(), re.data(), im.data(), n, kinds, vals, dcrs, counts, 3, 5);
        double wrmse = -1, devices = 0;
        CHECK(hasStr(out, "\"ok\":true"), "try2 ok");
        CHECK(jsonNum(out, "wrmse", 0, &wrmse) && wrmse < 1e-9, "try2 best wRMSE ~ 0");
        CHECK(jsonNum(out, "devices", 0, &devices) && devices == 3, "try2 devices == 3");
        CHECK(hasStr(out, "\"sp\":true") || hasStr(out, "\"sp\":false"), "try2 sp flag present");
        CHECK(hasStr(out, "n_funnel_kept"), "try2 stats present");
        lcr_free(out);
    }
    // resistor-only multiset rejected (A4)
    {
        const char kinds[1] = {'R'};
        const double vals[1] = {100.0};
        const double dcrs[1] = {0.0};
        const int counts[1] = {2};
        char* out = lcr_try2(f.data(), re.data(), im.data(), n, kinds, vals, dcrs, counts, 1, 5);
        CHECK(hasStr(out, "\"ok\":false") && hasStr(out, "bad_input"), "try2 rejects resistor-only set");
        lcr_free(out);
    }

    // ---------------- try3 ----------------
    // ladder: 0 --L-- 2 --C-- 1, 2 --R-- 1  =>  Z = ZL + (Zc || R)
    {
        std::vector<double> r3(n), i3(n);
        for (int i = 0; i < n; ++i) {
            C s(0.0, 2.0 * M_PI * f[i]);
            C zl = DCR + s * L;
            C zc = 1.0 / (s * Cc);
            C zr(50.0, 0.0);
            C z = zl + zc * zr / (zc + zr);
            r3[i] = z.real();
            i3[i] = z.imag();
        }
        const int us[3] = {0, 1, 1};
        const int vs[3] = {2, 2, 2};
        const char kinds[3] = {'L', 'C', 'R'};
        char* out = lcr_try3(f.data(), r3.data(), i3.data(), n, us, vs, kinds, 3);
        double wrmse = -1, v1 = 0;
        CHECK(hasStr(out, "\"ok\":true"), "try3 ok");
        CHECK(jsonNum(out, "wrmse", 0, &wrmse) && wrmse < 1e-6, "try3 wRMSE ~ 0");
        CHECK(hasStr(out, "\"groups\":[") && hasStr(out, "\"jac_rank\""), "try3 diagnostics present");
        CHECK(hasStr(out, "\"notes\":["), "try3 notes present");
        (void)v1;
        lcr_free(out);
    }
    // port-open topology: edges only among internal nodes
    {
        std::vector<double> r3(n, 1.0), i3(n, 0.0);
        const int us[2] = {0, 2};
        const int vs[2] = {2, 3};
        const char kinds[2] = {'R', 'R'};
        char* out = lcr_try3(f.data(), r3.data(), i3.data(), n, us, vs, kinds, 2);
        CHECK(hasStr(out, "\"ok\":false") &&
                  (hasStr(out, "port_open") || hasStr(out, "bad_input")),
              "try3 rejects topology without port 1");
        lcr_free(out);
    }

    if (g_fail) {
        std::printf("\n%d FAILURE(S)\n", g_fail);
        return 1;
    }
    std::printf("\nall glue tests passed\n");
    return 0;
}
