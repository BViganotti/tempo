#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>

namespace tempo::test {

/// RAII temporary directory under the system temp root. Never uses the network
/// or the user's home directory.
class TempDir {
 public:
  TempDir() {
    std::error_code ec;
    const auto base = std::filesystem::temp_directory_path(ec);
    std::filesystem::path pattern =
        (ec ? std::filesystem::path("/tmp") : base) / "tempo-test-XXXXXX";
    std::string templ = pattern.string();
    // mkdtemp requires a mutable buffer.
    std::vector<char> buffer(templ.begin(), templ.end());
    buffer.push_back('\0');
    if (::mkdtemp(buffer.data()) != nullptr) {
      path_ = buffer.data();
    }
  }
  ~TempDir() {
    if (!path_.empty()) {
      std::error_code ec;
      std::filesystem::remove_all(path_, ec);
    }
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  const std::string& path() const { return path_; }

  bool write(const std::string& relative, const std::string& content) const {
    std::ofstream out(path_ + "/" + relative, std::ios::binary);
    if (!out) {
      return false;
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(out);
  }

  std::string read(const std::string& relative) const {
    std::ifstream in(path_ + "/" + relative, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }

  bool symlink(const std::string& relative, const std::string& target) const {
    return ::symlink(target.c_str(), (path_ + "/" + relative).c_str()) == 0;
  }

  bool exists(const std::string& relative) const {
    std::error_code ec;
    return std::filesystem::exists(path_ + "/" + relative, ec);
  }

 private:
  std::string path_;
};

}  // namespace tempo::test
