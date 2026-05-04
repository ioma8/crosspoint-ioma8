#include "LanguageRegistry.h"

#include <algorithm>
#include <array>

#include "HyphenationCommon.h"
#ifndef CROSSPOINT_SLIM
#include "generated/hyph-de.trie.h"
#include "generated/hyph-en.trie.h"
#include "generated/hyph-es.trie.h"
#include "generated/hyph-fr.trie.h"
#include "generated/hyph-it.trie.h"
#include "generated/hyph-ru.trie.h"
#include "generated/hyph-uk.trie.h"
#endif

namespace {

#ifndef CROSSPOINT_SLIM
// English hyphenation patterns (3/3 minimum prefix/suffix length)
LanguageHyphenator englishHyphenator(en_patterns, isLatinLetter, toLowerLatin, 3, 3);
LanguageHyphenator frenchHyphenator(fr_patterns, isLatinLetter, toLowerLatin);
LanguageHyphenator germanHyphenator(de_patterns, isLatinLetter, toLowerLatin);
LanguageHyphenator russianHyphenator(ru_patterns, isCyrillicLetter, toLowerCyrillic);
LanguageHyphenator spanishHyphenator(es_patterns, isLatinLetter, toLowerLatin);
LanguageHyphenator italianHyphenator(it_patterns, isLatinLetter, toLowerLatin);
LanguageHyphenator ukrainianHyphenator(uk_patterns, isCyrillicLetter, toLowerCyrillic);

using EntryArray = std::array<LanguageEntry, 7>;
#else
// Slim build: no hyphenation trie data. Pattern-based hyphenation returns no
// breaks — same behavior as having hyphenation disabled in settings.
// Named static avoids dangling reference (LanguageHyphenator stores a ref).
static constexpr SerializedHyphenationPatterns kNullPatterns{0, nullptr, 0};
LanguageHyphenator nullHyphenator(kNullPatterns, isLatinLetter, toLowerLatin);
using EntryArray = std::array<LanguageEntry, 1>;
#endif

const EntryArray& entries() {
#ifndef CROSSPOINT_SLIM
  static const EntryArray kEntries = {{{"english", "en", &englishHyphenator},
                                       {"french", "fr", &frenchHyphenator},
                                       {"german", "de", &germanHyphenator},
                                       {"russian", "ru", &russianHyphenator},
                                       {"spanish", "es", &spanishHyphenator},
                                       {"italian", "it", &italianHyphenator},
                                       {"ukrainian", "uk", &ukrainianHyphenator}}};
#else
  static constexpr EntryArray kEntries = {{{"", "", &nullHyphenator}}};
#endif
  return kEntries;
}

}  // namespace

const LanguageHyphenator* getLanguageHyphenatorForPrimaryTag(const std::string& primaryTag) {
  (void)primaryTag;
#ifndef CROSSPOINT_SLIM
  const auto& allEntries = entries();
  const auto it = std::find_if(allEntries.begin(), allEntries.end(),
                               [&primaryTag](const LanguageEntry& entry) { return primaryTag == entry.primaryTag; });
  return (it != allEntries.end()) ? it->hyphenator : nullptr;
#else
  return &nullHyphenator;
#endif
}

LanguageEntryView getLanguageEntries() {
  const auto& allEntries = entries();
  return LanguageEntryView{allEntries.data(), allEntries.size()};
}
