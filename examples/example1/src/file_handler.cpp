#include <cstddef>
#include <cstring>
#include <file_handler.hpp>
#include <fstream>
#include <iostream>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <magic.h>
#include <memory>
#include <stdexcept>
#include <utility>
#include <zlib.h>
namespace files {

const size_t CHUNK_SIZE = 16000;

namespace {

// runs inflate, clearing out out_buf until done
size_t inflateChunk(z_stream *infstream, unsigned char *buf) {
  size_t total_read = 0;
  while (infstream->avail_in != 0) {
    infstream->next_out = buf;
    infstream->avail_out = CHUNK_SIZE;
    auto res = inflate(infstream, Z_BLOCK);
    if (res != Z_OK && res != Z_STREAM_END) {
      std::cerr << "Error zlib: " << res << std::endl;
      return 0;
    }
    total_read += infstream->next_out - buf;
  }
  return total_read;
}

size_t read_up_to(std::ifstream &strm, unsigned char *buf, size_t sz) {
  size_t rd = 0;
  auto curr_buf = buf;
  auto remain = sz;
  while (remain > 0 && !strm.eof()) {
    strm.read((char *)buf, remain);
    auto ct = strm.gcount();
    if (ct == 0) {
      return rd;
    }
    std::cout << "read " << ct << std::endl;
    rd += ct;
    remain -= ct;
    curr_buf += ct;
  }
  return rd;
}
} // namespace

bool ZlibCallback::processFile(const std::filesystem::path &target) {
  std::ifstream strm(target);
  unsigned char *in_buf = (unsigned char *)malloc(CHUNK_SIZE);
  unsigned char *out_buf = (unsigned char *)malloc(CHUNK_SIZE);
  z_stream infstream;
  infstream.zalloc = Z_NULL;
  infstream.zfree = Z_NULL;
  infstream.opaque = Z_NULL;
  infstream.avail_in = 0;
  infstream.next_in = in_buf;
  infstream.avail_out = CHUNK_SIZE;
  infstream.next_out = out_buf;

  infstream.avail_in = read_up_to(strm, in_buf, CHUNK_SIZE);
  // Initialization special for gzip
  inflateInit2(&infstream, 16 + MAX_WBITS);
  gz_header header;
  memset(&header, 0, sizeof(header));
  inflateGetHeader(&infstream, &header);
  std::cout << "Header listed timestamp: " << header.time << std::endl;
  std::size_t total = 0;
  while (!strm.eof() || infstream.avail_in > 0) {
    if (infstream.avail_in > 0) {
      auto rd = inflateChunk(&infstream, out_buf);
      if (rd == 0) {
        return false;
      }
      total += rd;
      std::cout << "Finished inflate call current total: " << total
                << std::endl;
    } else {
      auto res = read_up_to(strm, in_buf, CHUNK_SIZE);
      if (res == 0) {
        std::cerr << "Failed to read chunk" << std::endl;
        return false;
      }

      infstream.avail_in = res;
    }
  }

  free(in_buf);
  free(out_buf);
  return true;
}

bool XmlCallback::processFile(const std::filesystem::path &target) {
  xmlDocPtr doc;
  xmlNodePtr root_element;

  doc = xmlReadFile(target.c_str(), NULL, 0);
  if (doc == NULL) {
    std::cerr << "Failed to parse xml file" << std::endl;
    return false;
  }

  root_element = xmlDocGetRootElement(doc);
  std::cout << "Root: " << root_element->name << std::endl;

  xmlFreeDoc(doc);
  xmlCleanupParser();
  return true;
}

FileHandler::FileHandler(const std::filesystem::path &target,
                         const std::filesystem::path &magic_path)
    : target(target), magic_path(magic_path) {}

std::string FileHandler::detect_mime_type() {
  // TODO is this big enough
  std::ifstream strm(this->target);
  unsigned char buff[300];
  read_up_to(strm, buff, 300);
  auto str = magic_path.empty() ? NULL : magic_path.c_str();
  magic_t magic = magic_open(MAGIC_MIME_TYPE);
  if (magic_load(magic, str) != 0) {
    std::cerr << "Failed to load magic.mgc" << std::endl;
    return "fail";
  }
  auto res = magic_buffer(magic, buff, strm.gcount());
  if (res == NULL) {
    std::cerr << "Magic buffer failed" << std::endl;
    return "fail";
  } else {
    std::cout << "Retrieved mimetype: " << res << std::endl;
  }
  auto s = std::string(res);
  magic_close(magic);
  return s;
}

bool TextCallback::processFile(const std::filesystem::path &target) {
  std::string line;
  std::ifstream strm(target);
  while (std::getline(strm, line)) {
    std::cout << line << std::endl;
  }
  return true;
}

bool FileHandler::processFile() {
  auto mtype = this->detect_mime_type();

  if (this->cbs.find(mtype) == this->cbs.end()) {
    throw std::runtime_error("No handler for returned mimetype");
  }

  return this->cbs.find(mtype)->second->processFile(this->target);
}

} // namespace files