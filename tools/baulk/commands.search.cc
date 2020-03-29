#include <bela/fnmatch.hpp>
#include <bela/ascii.hpp>
#include <version.hpp>
#include "baulk.hpp"
#include "bucket.hpp"
#include "commands.hpp"
#include "fs.hpp"

namespace baulk::commands {
// lldb/kali-rolling 1:9.0-49.1 amd64
//   Next generation, high-performance debugger

// package

class Searcher {
public:
  Searcher(const argv_t &argv_) {
    for (const auto a : argv_) {
      argv.emplace_back(bela::AsciiStrToLower(a));
    }
  }
  bool PkgMatch(std::wstring_view pkgname) {
    for (const auto &a : argv) {
      if (bela::FnMatch(a, pkgname)) {
        return true;
      }
    }
    return false;
  };
  bool SearchMatched(std::wstring_view bucketdir, std::wstring_view bucket) {
    baulk::fs::Finder finder;
    bela::error_code ec;
    if (!finder.First(bucketdir, L"*.json", ec)) {
      return false;
    }
    do {
      if (finder.Ignore()) {
        continue;
      }
      auto pkgname = finder.Name();
      auto pkgmeta = bela::StringCat(bucketdir, L"\\", pkgname);
      pkgname.remove_suffix(5);
      if (!PkgMatch(bela::AsciiStrToLower(pkgname))) {
        continue;
      }
      auto pkg = baulk::bucket::PackageMeta(pkgmeta, pkgname, bucket, ec);
      if (!pkg) {
        bela::FPrintF(stderr, L"Parse %s error: %s\n", pkgmeta, ec.message);
        continue;
      }
      auto lopkg = baulk::bucket::PackageLocalMeta(pkgname, ec);
      if (lopkg && bela::EndsWithIgnoreCase(lopkg->bucket, pkg->bucket)) {
        bela::FPrintF(stderr,
                      L"\x1b[32m%s\x1b[0m/\x1b[34m%s\x1b[0m %s [installed "
                      L"\x1b[33m%s\x1b[0m]\n  %s\n",
                      pkg->name, pkg->bucket, pkg->version, lopkg->version,
                      pkg->description);
      } else {
        bela::FPrintF(stderr, L"\x1b[32m%s\x1b[0m/\x1b[34m%s\x1b[0m %s\n  %s\n",
                      pkg->name, pkg->bucket, pkg->version, pkg->description);
      }
    } while (finder.Next());

    return true;
  }

private:
  std::vector<std::wstring> argv;
};

int cmd_search(const argv_t &argv) {
  if (argv.empty()) {
    bela::FPrintF(stderr, L"usage: baulk search package(or pattern)\n");
    return 1;
  }
  Searcher searcher(argv);
  auto buckets =
      bela::StringCat(baulk::BaulkRoot(), L"\\", baulk::BucketsDirName);
  for (const auto &bk : baulk::BaulkBuckets()) {
    auto bucketdir = bela::StringCat(buckets, L"\\", bk.name, L"\\bucket");
    searcher.SearchMatched(bucketdir, bk.name);
  }
  return 0;
}
} // namespace baulk::commands