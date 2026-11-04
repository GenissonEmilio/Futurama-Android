#include "core/FloatConvert.h"
#include "core/fs/Xdvdfs.h"
#include "core/memory/MemoryManager.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace {
int failures = 0;
void Check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
}

int main() {
    using namespace fut;
    using namespace fut::numeric;
    constexpr auto indefinite = std::numeric_limits<s32>::min();
    Check(X87RoundToInt32(0.5) == 0, "round nearest even below one");
    Check(X87RoundToInt32(1.5) == 2, "round nearest even above one");
    Check(X87RoundToInt32(-2.5) == -2, "negative ties round to even");
    Check(X87RoundToInt32(2147483647.0) == 2147483647, "round maximum signed integer");
    Check(X87RoundToInt32(2147483647.5) == indefinite, "rounded positive overflow");
    Check(X87RoundToInt32(-2147483648.5) == indefinite, "negative boundary tie");
    Check(X87RoundToInt32(std::numeric_limits<double>::quiet_NaN()) == indefinite, "round NaN");
    Check(X87RoundToInt32(std::numeric_limits<double>::infinity()) == indefinite, "round infinity");
    Check(X86TruncateToInt32(-1.9f) == -1, "truncate toward zero");
    Check(X86TruncateToInt32(2147483648.0f) == indefinite, "float signed overflow");
    Check(X86TruncateToInt32(std::numeric_limits<float>::quiet_NaN()) == indefinite, "truncate NaN");
    Check(fs::Xdvdfs::Normalize("DUMMY\\FoLdEr/File.XMV") == "dummy/folder/file.xmv", "Xbox case folding");
    Check(fs::Xdvdfs::Normalize("/SYSNTSC.IMG") == "sysntsc.img", "Xbox root path");
    fs::Xdvdfs disc;
    std::string error;
    Check(!disc.Open("futurama-nonexistent-test-image.iso", error) && !error.empty(), "missing disc error");
    std::printf("Portable runtime checks: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
