#include <Utf8.h>
#include <Utf8CodepointCollector.h>

#include <cstdio>
#include <string>

namespace {

int g_failures = 0;

void check(bool ok, const char* expr, const char* file, int line) {
  if (!ok) {
    std::fprintf(stderr, "FAIL %s:%d  (%s)\n", file, line, expr);
    ++g_failures;
  }
}

#define REQUIRE(cond) check(static_cast<bool>(cond), #cond, __FILE__, __LINE__)

void testDeduplicatesAndKeepsFirstSeenOrder() {
  Utf8CodepointCollector c;
  REQUIRE(c.add('a'));
  REQUIRE(c.add('b'));
  REQUIRE(c.add('a'));
  REQUIRE(c.add(0x00E1));   // á
  REQUIRE(c.add(0x1F642));  // 🙂
  REQUIRE(c.size() == 4);
  REQUIRE(c.toUtf8String() == std::string("ab\303\241\360\237\231\202"));
}

void testCapacityIsBounded() {
  Utf8CodepointCollector c;
  for (size_t i = 0; i < Utf8CodepointCollector::kMaxCodepoints; ++i) {
    REQUIRE(c.add(static_cast<uint32_t>(0x1000 + i)));
  }
  REQUIRE(c.full());
  REQUIRE(c.size() == Utf8CodepointCollector::kMaxCodepoints);
  REQUIRE(!c.add(0x200000));
  REQUIRE(c.size() == Utf8CodepointCollector::kMaxCodepoints);
}

void testUtf8RoundTripCanBeDecoded() {
  Utf8CodepointCollector c;
  REQUIRE(c.add('x'));
  REQUIRE(c.add(0x010D));  // č
  REQUIRE(c.add(0x20AC));  // €
  const std::string out = c.toUtf8String();
  const auto* p = reinterpret_cast<const unsigned char*>(out.c_str());
  REQUIRE(utf8NextCodepoint(&p) == 'x');
  REQUIRE(utf8NextCodepoint(&p) == 0x010D);
  REQUIRE(utf8NextCodepoint(&p) == 0x20AC);
  REQUIRE(utf8NextCodepoint(&p) == 0);
}

}  // namespace

int main() {
  testDeduplicatesAndKeepsFirstSeenOrder();
  testCapacityIsBounded();
  testUtf8RoundTripCanBeDecoded();
  if (g_failures != 0) {
    std::fprintf(stderr, "\n%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("All UTF-8 codepoint collector checks passed.\n");
  return 0;
}
