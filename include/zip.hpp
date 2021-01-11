//
#ifndef BAULK_ZIP_HPP
#define BAULK_ZIP_HPP
#include <span>
#include <bela/base.hpp>
#include <bela/buffer.hpp>
#include <bela/str_cat_narrow.hpp>
#include <bela/time.hpp>
#include <bela/phmap.hpp>
#include "archive.hpp"

namespace baulk::archive::zip {
// https://www.hanshq.net/zip.html
// https://github.com/nih-at/libzip/blob/master/lib/zip.h
typedef enum zip_method_e : uint16_t {
  ZIP_STORE = 0,    /* stored (uncompressed) */
  ZIP_SHRINK = 1,   /* shrunk */
  ZIP_REDUCE_1 = 2, /* reduced with factor 1 */
  ZIP_REDUCE_2 = 3, /* reduced with factor 2 */
  ZIP_REDUCE_3 = 4, /* reduced with factor 3 */
  ZIP_REDUCE_4 = 5, /* reduced with factor 4 */
  ZIP_IMPLODE = 6,  /* imploded */
  /* 7 - Reserved for Tokenizing compression algorithm */
  ZIP_DEFLATE = 8,         /* deflated */
  ZIP_DEFLATE64 = 9,       /* deflate64 */
  ZIP_PKWARE_IMPLODE = 10, /* PKWARE imploding */
  /* 11 - Reserved by PKWARE */
  ZIP_BZIP2 = 12, /* compressed using BZIP2 algorithm */
  /* 13 - Reserved by PKWARE */
  ZIP_LZMA = 14, /* LZMA (EFS) */
  /* 15-17 - Reserved by PKWARE */
  ZIP_TERSE = 18, /* compressed using IBM TERSE (new) */
  ZIP_LZ77 = 19,  /* IBM LZ77 z Architecture (PFS) */
  /* 20 - old value for Zstandard */
  /* ZIP LZMA2*/
  ZIP_LZMA2 = 33,
  ZIP_ZSTD = 93,    /* Zstandard compressed data */
  ZIP_MP3 = 94,     /* MP3 compression data */
  ZIP_XZ = 95,      /* XZ compressed data */
  ZIP_JPEG = 96,    /* Compressed Jpeg data */
  ZIP_WAVPACK = 97, /* WavPack compressed data */
  ZIP_PPMD = 98,    /* PPMd version I, Rev 1 */
  ZIP_AES = 99,     /* AE-x encryption marker (see APPENDIX E) */

  // Private magic number
  ZIP_BROTLI = 121,
} zip_method_t;

struct directoryEnd {
  uint32_t diskNbr{0};            // unused
  uint32_t dirDiskNbr{0};         // unused
  uint64_t dirRecordsThisDisk{0}; // unused
  uint64_t directoryRecords{0};
  uint64_t directorySize{0};
  uint64_t directoryOffset{0}; // relative to file
  uint16_t commentLen;
  std::string comment;
};

using bela::os::FileMode;
// FileMode to string
std::string String(FileMode m);

struct File {
  std::string name;
  std::string comment;
  uint64_t compressedSize{0};
  uint64_t uncompressedSize{0};
  uint64_t position{0}; // file position
  bela::Time time;
  uint32_t crc32sum{0};
  FileMode mode{0};
  uint16_t cversion{0};
  uint16_t rversion{0};
  uint16_t flags{0};
  uint16_t method{0};
  uint16_t aesVersion{0};
  uint8_t aesStrength{0};
  bool IsFileNameUTF8() const { return (flags & 0x800) != 0; }
  bool IsEncrypted() const { return (flags & 0x1) != 0; }
  bool IsDir() const { return (mode & FileMode::ModeDir) != 0; }
  bool IsSymlink() const { return (mode & FileMode::ModeSymlink) != 0; }
  bool StartsWith(std::string_view prefix) const { return name.starts_with(prefix); }
  bool EndsWith(std::string_view suffix) const { return name.ends_with(suffix); }
  bool Contains(char ch) const { return name.find(ch) != std::string::npos; }
  bool Contains(std::string_view sv) { return name.find(sv) != std::string::npos; }
};

constexpr static auto size_max = (std::numeric_limits<std::size_t>::max)();

using Receiver = std::function<bool(const void *data, size_t len)>;
class Reader {
private:
  bool PositionAt(int64_t pos, bela::error_code &ec) const {
    auto li = *reinterpret_cast<LARGE_INTEGER *>(&pos);
    LARGE_INTEGER oli{0};
    if (SetFilePointerEx(fd, li, &oli, SEEK_SET) != TRUE) {
      ec = bela::make_system_error_code(L"SetFilePointerEx: ");
      return false;
    }
    return true;
  }
  bool ReadFull(void *buffer, size_t len, bela::error_code &ec) const {
    auto p = reinterpret_cast<uint8_t *>(buffer);
    size_t total = 0;
    while (total < len) {
      DWORD dwSize = 0;
      if (ReadFile(fd, p + total, static_cast<DWORD>(len - total), &dwSize, nullptr) != TRUE) {
        ec = bela::make_system_error_code(L"ReadFile: ");
        return false;
      }
      if (dwSize == 0) {
        ec = bela::make_error_code(ERROR_HANDLE_EOF, L"Reached the end of the file");
        return false;
      }
      total += dwSize;
    }
    return true;
  }
  // ReadAt ReadFull
  bool ReadAt(void *buffer, size_t len, int64_t pos, bela::error_code &ec) const {
    if (!PositionAt(pos, ec)) {
      return false;
    }
    return ReadFull(buffer, len, ec);
  }
  void Free() {
    if (needClosed && fd != INVALID_HANDLE_VALUE) {
      CloseHandle(fd);
      fd = INVALID_HANDLE_VALUE;
    }
  }
  void MoveFrom(Reader &&r) {
    Free();
    fd = r.fd;
    r.fd = INVALID_HANDLE_VALUE;
    needClosed = r.needClosed;
    r.needClosed = false;
    size = r.size;
    r.size = 0;
    uncompressedSize = r.uncompressedSize;
    r.uncompressedSize = 0;
    compressedSize = r.compressedSize;
    r.compressedSize = 0;
    comment = std::move(r.comment);
    files = std::move(r.files);
  }

public:
  Reader() = default;
  Reader(Reader &&r) noexcept { MoveFrom(std::move(r)); }
  Reader &operator=(Reader &&r) noexcept {
    MoveFrom(std::move(r));
    return *this;
  }
  ~Reader() { Free(); }
  bool OpenReader(std::wstring_view file, bela::error_code &ec);
  bool OpenReader(HANDLE nfd, int64_t sz, bela::error_code &ec);
  Reader &Credential(std::string_view password) {
    passwd.assign(password);
    return *this;
  }
  std::string_view Comment() const { return comment; }
  const auto &Files() const { return files; }
  int64_t CompressedSize() const { return compressedSize; }
  int64_t UncompressedSize() const { return uncompressedSize; }
  static std::optional<Reader> NewReader(HANDLE fd, int64_t sz, bela::error_code &ec) {
    Reader r;
    if (!r.OpenReader(fd, sz, ec)) {
      return std::nullopt;
    }
    return std::make_optional(std::move(r));
  }
  bool Decompress(const File &file, const Receiver &receiver, int64_t &decompressed, bela::error_code &ec) const;

private:
  std::string passwd;
  std::string comment;
  std::vector<File> files;
  HANDLE fd{INVALID_HANDLE_VALUE};
  int64_t size{bela::SizeUnInitialized};
  int64_t uncompressedSize{0};
  int64_t compressedSize{0};
  bool needClosed{false};
  bool Initialize(bela::error_code &ec);
  bool readDirectoryEnd(directoryEnd &d, bela::error_code &ec);
  bool readDirectory64End(int64_t offset, directoryEnd &d, bela::error_code &ec);
  int64_t findDirectory64End(int64_t directoryEndOffset, bela::error_code &ec);
  bool decompressDeflate(const File &file, const Receiver &receiver, int64_t &decompressed, bela::error_code &ec) const;
  bool decompressDeflate64(const File &file, const Receiver &receiver, int64_t &decompressed,
                           bela::error_code &ec) const;
  bool decompressZstd(const File &file, const Receiver &receiver, int64_t &decompressed, bela::error_code &ec) const;
  bool decompressBz2(const File &file, const Receiver &receiver, int64_t &decompressed, bela::error_code &ec) const;
  bool decompressXz(const File &file, const Receiver &receiver, int64_t &decompressed, bela::error_code &ec) const;
  bool decompressLZMA(const File &file, const Receiver &receiver, int64_t &decompressed, bela::error_code &ec) const;
  bool decompressLZMA2(const File &file, const Receiver &receiver, int64_t &decompressed, bela::error_code &ec) const;
  bool decompressPpmd(const File &file, const Receiver &receiver, int64_t &decompressed, bela::error_code &ec) const;
  bool decompressBrotli(const File &file, const Receiver &receiver, int64_t &decompressed, bela::error_code &ec) const;
};

// NewReader
inline std::optional<Reader> NewReader(HANDLE fd, int64_t size, bela::error_code &ec) {
  return Reader::NewReader(fd, size, ec);
}

// root must be the cleaned path
std::optional<std::wstring> PathCat(std::wstring_view root, const File &file, bool autocvt = true);

//
} // namespace baulk::archive::zip

#endif