#pragma once
#include "minikv/lru_cache.hpp"
#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace minikv {
enum class Operation : std::uint8_t { Put = 1, Delete = 2 };

struct Record {
  Operation operation;
  std::string key;
  std::string value;
  std::uint64_t sequence;
};

struct Stats {
  std::uint64_t puts = 0, gets = 0, deletes = 0;
  std::uint64_t cache_hits = 0, cache_misses = 0;
  std::uint64_t recovered_records = 0;
};

class StorageEngine {
 public:
  explicit StorageEngine(std::string path, std::size_t cache_capacity = 1024,
                         bool sync_on_write = false);
  ~StorageEngine();
  StorageEngine(const StorageEngine&) = delete;
  StorageEngine& operator=(const StorageEngine&) = delete;

  void put(const std::string& key, const std::string& value);
  std::optional<std::string> get(const std::string& key);
  bool erase(const std::string& key);
  void apply(const Record& record);
  void flush();
  std::size_t size() const;
  Stats stats() const;
  std::uint64_t sequence() const;

 private:
  void recover();
  void append(const Record& record);
  void apply_in_memory(const Record& record);

  std::string path_;
  bool sync_on_write_;
  mutable std::shared_mutex mu_;
  std::unordered_map<std::string, std::string> index_;
  LruCache cache_;
  std::ofstream log_;
  std::uint64_t sequence_ = 0;
  Stats stats_;
};
}  // namespace minikv

