///
#ifndef BAULK_ARCHIVE_EXTRACTOR_HPP
#define BAULK_ARCHIVE_EXTRACTOR_HPP
#include <bela/terminal.hpp>
#include <baulk/archive.hpp>
#include <baulk/indicators.hpp>
#include <filesystem>
#include "zip.hpp"

namespace baulk::archive {
class ZipExtractor {
public:
  ZipExtractor(bool quietMode_ = false) noexcept {
    quietMode = quietMode_;
    if (!quietMode) {
      if (bela::terminal::IsSameTerminal(stderr)) {
        if (auto cygwinterminal = bela::terminal::IsCygwinTerminal(stderr); cygwinterminal) {
          CygwinTerminalSize(termsz);
          return;
        }
        bela::terminal::TerminalSize(stderr, termsz);
      }
    }
  }
  ZipExtractor(const ZipExtractor &) = delete;
  ZipExtractor &operator=(const ZipExtractor &) = delete;
  std::wstring &Destination() { return destination; }
  bool OpenReader(std::wstring_view file, std::wstring_view dest, bela::error_code &ec) {
    auto zipfile = bela::PathAbsolute(file);
    destination = bela::PathAbsolute(dest);
    return reader.OpenReader(zipfile, ec);
  }

  bool OpenReader(bela::io::FD &fd, std::wstring_view dest, int64_t size, int64_t offset, bela::error_code &ec) {
    destination = bela::PathAbsolute(dest);
    return reader.OpenReader(fd.NativeFD(), size, offset, ec);
  }

  bool Extract(bela::error_code &ec) {
    destsize = destination.size() + 1;
    for (const auto &file : reader.Files()) {
      if (!extractFile(file, ec)) {
        return false;
      }
    }
    if (!quietMode) {
      bela::FPrintF(stderr, L"\n");
    }
    return true;
  }

private:
  zip::Reader reader;
  std::wstring destination;
  bela::terminal::terminal_size termsz{0};
  int64_t decompressed{0};
  size_t destsize{0};
  bool quietMode{false};
  bool owfile{true};
  bool extractFile(const zip::File &file, bela::error_code &ec);
  bool extractDir(const zip::File &file, std::wstring_view dir, bela::error_code &ec);
  bool extractSymlink(const zip::File &file, std::wstring_view filename, bela::error_code &ec);
  void showProgress(const std::wstring_view filename) {
    auto suglen = static_cast<size_t>(termsz.columns) - 8;
    if (auto n = bela::string_width<wchar_t>(filename); n <= suglen) {
      bela::FPrintF(stderr, L"\x1b[2K\r\x1b[33mx %s\x1b[0m", filename);
      return;
    }
    bela::FPrintF(stderr, L"\x1b[2K\r\x1b[33mx ...\\%s\x1b[0m", bela::BaseName(filename));
  }
};

inline bool ZipExtractor::extractSymlink(const zip::File &file, std::wstring_view filename, bela::error_code &ec) {
  std::string linkname;
  auto ret = reader.Decompress(
      file,
      [&](const void *data, size_t len) {
        linkname.append(reinterpret_cast<const char *>(data), len);
        return true;
      },
      ec);
  if (!ret) {
    return false;
  }
  auto wn = bela::encode_into<char, wchar_t>(linkname);
  if (!baulk::archive::NewSymlink(filename, wn, ec, owfile)) {
    return false;
  }
  return true;
}

inline bool ZipExtractor::extractDir(const zip::File &file, std::wstring_view dir, bela::error_code &ec) {
  if (bela::PathExists(dir, bela::FileAttribute::Dir)) {
    return true;
  }
  std::error_code e;
  if (!std::filesystem::create_directories(dir, e)) {
    ec = bela::from_std_error_code(e, L"mkdir ");
    return false;
  }
  baulk::archive::Chtimes(dir, file.time, ec);
  return true;
}

inline bool ZipExtractor::extractFile(const zip::File &file, bela::error_code &ec) {
  auto dest = baulk::archive::JoinSanitizePath(destination, file.name, file.IsFileNameUTF8());
  if (!dest) {
    bela::FPrintF(stderr, L"skip dangerous path %s\n", file.name);
    return true;
  }
  auto showName = std::wstring_view(dest->data() + destsize, dest->size() - destsize);
  if (termsz.columns != 0) {
    showProgress(showName);
  }
  if (file.IsSymlink()) {
    return extractSymlink(file, *dest, ec);
  }
  if (file.IsDir()) {
    return extractDir(file, *dest, ec);
  }
  auto fd = baulk::archive::File::NewFile(*dest, owfile, ec);
  if (!fd) {
    bela::FPrintF(stderr, L"unable NewFD %s error: %s\n", *dest, ec);
    return false;
  }
  if (!fd->Chtimes(file.time, ec)) {
    bela::FPrintF(stderr, L"unable SetTime %s error: %s\n", *dest, ec);
    return false;
  }
  bela::error_code writeEc;
  auto ret = reader.Decompress(
      file,
      [&](const void *data, size_t len) {
        //
        return fd->WriteFull(data, len, writeEc);
      },
      ec);
  if (!ret) {
    bela::FPrintF(stderr, L"unable decompress %s error: %s (%s)\n", *dest, ec, writeEc);
    return false;
  }
  return true;
}

} // namespace baulk::archive

#endif