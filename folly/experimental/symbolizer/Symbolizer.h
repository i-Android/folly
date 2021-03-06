/*
 * Copyright 2013 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#ifndef FOLLY_EXPERIMENTAL_SYMBOLIZER_SYMBOLIZER_H_
#define FOLLY_EXPERIMENTAL_SYMBOLIZER_SYMBOLIZER_H_

#include <cstdint>
#include <string>
#include <unordered_map>

#include "folly/FBString.h"
#include "folly/Range.h"
#include "folly/experimental/symbolizer/Elf.h"
#include "folly/experimental/symbolizer/Dwarf.h"
#include "folly/experimental/symbolizer/StackTrace.h"

namespace folly {
namespace symbolizer {

/**
 * Frame information: symbol name and location.
 *
 * Note that both name and location are references in the Symbolizer object,
 * which must outlive this SymbolizedFrame object.
 */
struct SymbolizedFrame {
  SymbolizedFrame() : found(false) { }
  bool isSignalFrame;
  bool found;
  StringPiece name;
  Dwarf::LocationInfo location;
};

template <size_t N>
struct FrameArray {
  FrameArray() : frameCount(0) { }

  size_t frameCount;
  uintptr_t addresses[N];
  SymbolizedFrame frames[N];
};

/**
 * Get stack trace into a given FrameArray, return true on success (and
 * set frameCount to the actual frame count, which may be > N) and false
 * on failure.
 */
namespace detail {
template <size_t N>
bool fixFrameArray(FrameArray<N>& fa, ssize_t n) {
  if (n != -1) {
    fa.frameCount = n;
    for (size_t i = 0; i < fa.frameCount; ++i) {
      fa.frames[i].found = false;
    }
    return true;
  } else {
    fa.frameCount = 0;
    return false;
  }
}
}  // namespace detail

template <size_t N>
bool getStackTrace(FrameArray<N>& fa) {
  return detail::fixFrameArray(fa, getStackTrace(fa.addresses, N));
}

template <size_t N>
bool getStackTraceSafe(FrameArray<N>& fa) {
  return detail::fixFrameArray(fa, getStackTraceSafe(fa.addresses, N));
}

class Symbolizer {
 public:
  Symbolizer() : fileCount_(0) { }

  /**
   * Symbolize given addresses.
   */
  void symbolize(const uintptr_t* addresses,
                 SymbolizedFrame* frames,
                 size_t frameCount);

  template <size_t N>
  void symbolize(FrameArray<N>& fa) {
    symbolize(fa.addresses, fa.frames, fa.frameCount);
  }

  /**
   * Shortcut to symbolize one address.
   */
  bool symbolize(uintptr_t address, SymbolizedFrame& frame) {
    symbolize(&address, &frame, 1);
    return frame.found;
  }

 private:
  // We can't allocate memory, so we'll preallocate room.
  // "1023 shared libraries should be enough for everyone"
  static constexpr size_t kMaxFiles = 1024;
  size_t fileCount_;
  ElfFile files_[kMaxFiles];
};

/**
 * Print a list of symbolized addresses. Base class.
 */
class SymbolizePrinter {
 public:
  /**
   * Print one address, no ending newline.
   */
  void print(uintptr_t address, const SymbolizedFrame& frame);

  /**
   * Print one address with ending newline.
   */
  void println(uintptr_t address, const SymbolizedFrame& frame);

  /**
   * Print multiple addresses on separate lines.
   */
  void println(const uintptr_t* addresses,
               const SymbolizedFrame* frames,
               size_t frameCount);

  /**
   * Print multiple addresses on separate lines, skipping the first
   * skip addresses.
   */
  template <size_t N>
  void println(const FrameArray<N>& fa, size_t skip=0) {
    if (skip < fa.frameCount) {
      println(fa.addresses + skip, fa.frames + skip, fa.frameCount - skip);
    }
  }

  virtual ~SymbolizePrinter() { }

  enum Options {
    // Skip file and line information
    NO_FILE_AND_LINE = 1 << 0,

    // As terse as it gets: function name if found, address otherwise
    TERSE = 1 << 1,
  };

 protected:
  explicit SymbolizePrinter(int options) : options_(options) { }
  const int options_;

 private:
  void printTerse(uintptr_t address, const SymbolizedFrame& frame);
  virtual void doPrint(StringPiece sp) = 0;
};

/**
 * Print a list of symbolized addresses to a stream.
 * Not reentrant. Do not use from signal handling code.
 */
class OStreamSymbolizePrinter : public SymbolizePrinter {
 public:
  explicit OStreamSymbolizePrinter(std::ostream& out, int options=0)
    : SymbolizePrinter(options),
      out_(out) { }
 private:
  void doPrint(StringPiece sp) override;
  std::ostream& out_;
};

/**
 * Print a list of symbolized addresses to a file descriptor.
 * Ignores errors. Async-signal-safe.
 */
class FDSymbolizePrinter : public SymbolizePrinter {
 public:
  explicit FDSymbolizePrinter(int fd, int options=0)
    : SymbolizePrinter(options),
      fd_(fd) { }
 private:
  void doPrint(StringPiece sp) override;
  int fd_;
};

/**
 * Print a list of symbolized addresses to a FILE*.
 * Ignores errors. Not reentrant. Do not use from signal handling code.
 */
class FILESymbolizePrinter : public SymbolizePrinter {
 public:
  explicit FILESymbolizePrinter(FILE* file, int options=0)
    : SymbolizePrinter(options),
      file_(file) { }
 private:
  void doPrint(StringPiece sp) override;
  FILE* file_;
};

/**
 * Print a list of symbolized addresses to a std::string.
 * Not reentrant. Do not use from signal handling code.
 */
class StringSymbolizePrinter : public SymbolizePrinter {
 public:
  explicit StringSymbolizePrinter(int options=0) : SymbolizePrinter(options) { }

  std::string str() const { return buf_.toStdString(); }
  const fbstring& fbstr() const { return buf_; }
  fbstring moveFbString() { return std::move(buf_); }

 private:
  void doPrint(StringPiece sp) override;
  fbstring buf_;
};

}  // namespace symbolizer
}  // namespace folly

#endif /* FOLLY_EXPERIMENTAL_SYMBOLIZER_SYMBOLIZER_H_ */

