#pragma once
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
namespace files {
class MimeCallback {
public:
  virtual ~MimeCallback() = default;
  virtual bool processFile(const std::filesystem::path &target) = 0;
};

class ZlibCallback : public MimeCallback {
public:
  virtual bool processFile(const std::filesystem::path &target) override;
};

class XmlCallback : public MimeCallback {
public:
  virtual bool processFile(const std::filesystem::path &target) override;
};

class TextCallback : public MimeCallback {
public:
  virtual bool processFile(const std::filesystem::path &target) override;
};

class FileHandler {
private:
  const std::filesystem::path &target;
  const std::filesystem::path &magic_path;
  std::unordered_map<std::string, std::unique_ptr<MimeCallback>> cbs;

public:
  FileHandler(const std::filesystem::path &target,
              const std::filesystem::path &magic_path);
  std::string detect_mime_type();
  bool processFile();

  template <typename T, typename... Args>
  void registerCallback(const std::string &mimetype, Args... args) {
    std::cout << "Adding support for mimetype: " << mimetype << std::endl;
    this->cbs.emplace(mimetype, std::make_unique<T>(std::forward(args)...));
  }
};
} // namespace files
