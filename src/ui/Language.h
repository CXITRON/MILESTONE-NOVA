#pragma once
#include <cstddef>
#include <cstdint>
namespace nova {
// Screen language. Korean is the default; Siron replaces the Korean screen text listed in
// assets/siron/ui-strings.tsv. English text, lyrics, track metadata and portal text stay as is.
enum class Language : uint8_t { Korean, Siron };
void setLanguage(Language language);
Language language();
// Exact Korean screen text (including printf formats such as "%d일 남음") -> current language.
// Unknown text is returned unchanged.
const char *tr(const char *korean);
// Already formatted text such as a notice or update status: matches the table's printf formats
// and keeps the substituted values. Returns false when nothing matched or out is too small;
// the caller then shows the original text.
bool trFormatted(const char *text, char *out, size_t capacity);
namespace siron {
struct Phrase {
  const char *korean, *siron;
};
extern const Phrase phrases[];
extern const unsigned phraseCount;
} // namespace siron
} // namespace nova
