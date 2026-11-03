#pragma once
#include <cstddef>
#include <cstdint>
namespace nova::morse {
// Turns OK key edges into dots and dashes. The dot/dash split adapts to the person keying:
// once recent presses include both lengths (one at least twice the other), the split moves to
// their geometric mean, so a slow beginner and a practiced hand are both read correctly.
class Keyer {
public:
  static constexpr uint32_t startSplit = 240, minSplit = 100, maxSplit = 800;
  static constexpr size_t maxCode = 7, history = 8;
  void reset();
  void down(uint32_t at);
  void up(uint32_t at);
  // The key has been up for a letter gap (two split lengths, about four dots) after some code.
  bool letterDue(uint32_t now) const;
  const char *code() const { return code_; }
  bool empty() const { return !length_; }
  bool pressed() const { return down_; }
  bool overflow() const { return overflow_; }
  uint32_t split() const { return split_; } // Presses shorter than this are dots (ms).
  uint32_t heldFor(uint32_t now) const { return down_ ? now - downAt_ : 0; }
  void clear(); // Drops the pending code; keeps the learned speed.

private:
  char code_[maxCode + 1]{};
  uint8_t length_ = 0, recent_ = 0, next_ = 0;
  bool down_ = false, overflow_ = false;
  uint32_t downAt_ = 0, upAt_ = 0, split_ = startSplit;
  uint32_t durations_[history]{};
};
// International Morse letters, digits and common punctuation; nullptr when unknown.
const char *latin(const char *code);
// Korean Morse jamo as Hangul compatibility jamo (U+3131..U+3163); 0 when unknown.
uint32_t jamo(const char *code);
// Builds one Hangul syllable at a time from jamo, the way a two-set keyboard does: doubled
// consonants (ㄱㄱ=ㄲ), compound vowels (ㅗㅏ=ㅘ) and finals (ㄹㄱ=ㄺ), and a final consonant
// moves to the next syllable when a vowel follows (각+ㅏ = 가가).
class Composer {
public:
  static constexpr size_t maxJamo = 6;
  // Adds a jamo and returns the syllable it closed (0 if the open syllable took it).
  uint32_t feed(uint32_t jamo);
  bool erase();             // Removes the last jamo of the open syllable; false when empty.
  uint32_t flush();         // Closes the open syllable and returns it (0 if empty).
  void seal() { sealed_ = length_ > 0; } // Next jamo starts a new syllable.
  uint32_t current() const; // Open syllable or lone jamo; 0 when empty.
  bool empty() const { return !length_; }
  void clear() {
    length_ = 0;
    sealed_ = false;
  }

private:
  uint32_t jamo_[maxJamo]{};
  uint8_t length_ = 0;
  bool sealed_ = false;
};
// Memo text being keyed: committed UTF-8 plus the open Hangul syllable.
class Editor {
public:
  static constexpr size_t capacity = 1024; // Bytes of UTF-8 text, including the open syllable.
  enum class Result : uint8_t { Added, Unknown, Full };
  void clear();
  bool hangul() const { return hangul_; }
  void toggleAlphabet();
  Result letter(const char *code);
  Result space();
  void erase(); // The open syllable's last jamo, else the character before the cursor.
  void seal() { composer_.seal(); }
  // Starts editing saved text (cut at a character boundary to fit) with the cursor at the end.
  void load(const char *utf8);
  // Close the open syllable and step the cursor one character; false at either end.
  bool left();
  bool right();
  bool atEnd() const { return cursor_ == length_; }
  bool composing() const { return !composer_.empty(); }
  // The text with the open syllable at the cursor, NUL-terminated; `cursor` gets the byte
  // offset just after the open syllable, where the caret is drawn.
  size_t preview(char *out, size_t size, size_t *cursor = nullptr) const;
  const char *finish(); // Closes the open syllable and returns the whole text.
  size_t bytes() const { return length_; }
  bool empty() const { return !length_ && composer_.empty(); }

private:
  bool append(uint32_t codepoint);
  bool append(const char *utf8);
  bool room(size_t extra) const;
  char text_[capacity + 1]{};
  size_t length_ = 0, cursor_ = 0; // Bytes; the cursor sits on a character boundary.
  Composer composer_;
  bool hangul_ = true;
};
size_t encodeUtf8(uint32_t codepoint, char (&out)[5]);
} // namespace nova::morse
