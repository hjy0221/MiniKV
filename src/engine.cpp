#include "minikv/engine.hpp"
#include <array>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace minikv {
namespace {
constexpr std::uint32_t kMagic = 0x4D4B5631;  // MKV1
constexpr std::size_t kHeaderSize = 4 + 1 + 3 + 8 + 4 + 4 + 4;
constexpr std::uint32_t kMaxFieldSize = 64U * 1024U * 1024U;

void put32(char* p, std::uint32_t v) { std::memcpy(p, &v, sizeof(v)); }
void put64(char* p, std::uint64_t v) { std::memcpy(p, &v, sizeof(v)); }
std::uint32_t get32(const char* p) { std::uint32_t v; std::memcpy(&v, p, 4); return v; }
std::uint64_t get64(const char* p) { std::uint64_t v; std::memcpy(&v, p, 8); return v; }

std::uint32_t checksum(const char* data, std::size_t size) {
  std::uint32_t h = 2166136261u;
  for (std::size_t i = 0; i < size; ++i) { h ^= static_cast<unsigned char>(data[i]); h *= 16777619u; }
  return h;
}

std::vector<char> encode(const Record& r) {
  std::vector<char> out(kHeaderSize + r.key.size() + r.value.size(), 0);
  put32(out.data(), kMagic);
  out[4] = static_cast<char>(r.operation);
  put64(out.data() + 8, r.sequence);
  put32(out.data() + 16, static_cast<std::uint32_t>(r.key.size()));
  put32(out.data() + 20, static_cast<std::uint32_t>(r.value.size()));
  std::memcpy(out.data() + kHeaderSize, r.key.data(), r.key.size());
  std::memcpy(out.data() + kHeaderSize + r.key.size(), r.value.data(), r.value.size());
  put32(out.data() + 24, checksum(out.data() + 4, out.size() - 4));
  return out;
}
}  // namespace

StorageEngine::StorageEngine(std::string path, std::size_t cache_capacity, bool sync_on_write)
    : path_(std::move(path)), sync_on_write_(sync_on_write), cache_(cache_capacity) {
  auto parent = std::filesystem::path(path_).parent_path();
  if (!parent.empty()) std::filesystem::create_directories(parent);
  recover();
  log_.open(path_, std::ios::binary | std::ios::app);
  if (!log_) throw std::runtime_error("cannot open log: " + path_);
}

StorageEngine::~StorageEngine() { try { flush(); } catch (...) {} }

void StorageEngine::append(const Record& record) {
  auto bytes = encode(record);
  log_.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!log_) throw std::runtime_error("write-ahead log write failed");
  if (sync_on_write_) log_.flush();
}

void StorageEngine::apply_in_memory(const Record& record) {
  if (record.operation == Operation::Put) {
    index_[record.key] = record.value;
    cache_.put(record.key, record.value);
  } else {
    index_.erase(record.key);
    cache_.erase(record.key);
  }
  if (record.sequence > sequence_) sequence_ = record.sequence;
}

void StorageEngine::put(const std::string& key, const std::string& value) {
  if (key.empty()) throw std::invalid_argument("key must not be empty");
  if (key.size() > kMaxFieldSize || value.size() > kMaxFieldSize) throw std::length_error("key/value too large");
  std::unique_lock<std::shared_mutex> lock(mu_);
  Record r{Operation::Put, key, value, sequence_ + 1};
  append(r);  // WAL before memory mutation
  apply_in_memory(r);
  ++stats_.puts;
}

std::optional<std::string> StorageEngine::get(const std::string& key) {
  std::unique_lock<std::shared_mutex> lock(mu_);
  if (auto hit = cache_.get(key)) {
    ++stats_.gets; ++stats_.cache_hits;
    return hit;
  }
  ++stats_.gets; ++stats_.cache_misses;
  auto it = index_.find(key);
  if (it == index_.end()) return std::nullopt;
  auto value = it->second;
  cache_.put(key, value);
  return value;
}

bool StorageEngine::erase(const std::string& key) {
  std::unique_lock<std::shared_mutex> lock(mu_);
  if (index_.find(key) == index_.end()) return false;
  Record r{Operation::Delete, key, {}, sequence_ + 1};
  append(r);
  apply_in_memory(r);
  ++stats_.deletes;
  return true;
}

void StorageEngine::apply(const Record& record) {
  std::unique_lock<std::shared_mutex> lock(mu_);
  if (record.sequence <= sequence_) return;
  append(record);
  apply_in_memory(record);
}

void StorageEngine::recover() {
  std::ifstream in(path_, std::ios::binary);
  if (!in) return;
  while (true) {
    std::array<char, kHeaderSize> h{};
    in.read(h.data(), static_cast<std::streamsize>(h.size()));
    if (in.gcount() == 0) break;
    if (in.gcount() != static_cast<std::streamsize>(h.size())) break;  // torn tail
    const auto key_size = get32(h.data() + 16), value_size = get32(h.data() + 20);
    if (get32(h.data()) != kMagic || key_size > kMaxFieldSize || value_size > kMaxFieldSize) break;
    std::vector<char> body(static_cast<std::size_t>(key_size) + value_size);
    in.read(body.data(), static_cast<std::streamsize>(body.size()));
    if (in.gcount() != static_cast<std::streamsize>(body.size())) break;
    std::vector<char> checked(h.begin() + 4, h.end());
    checked.insert(checked.end(), body.begin(), body.end());
    std::fill(checked.begin() + 20, checked.begin() + 24, 0);
    if (checksum(checked.data(), checked.size()) != get32(h.data() + 24)) break;
    Record r{static_cast<Operation>(h[4]),
             std::string(body.data(), key_size),
             std::string(body.data() + key_size, value_size), get64(h.data() + 8)};
    if (r.operation != Operation::Put && r.operation != Operation::Delete) break;
    apply_in_memory(r);
    ++stats_.recovered_records;
  }
}

void StorageEngine::flush() { std::unique_lock<std::shared_mutex> lock(mu_); if (log_) log_.flush(); }
std::size_t StorageEngine::size() const { std::shared_lock<std::shared_mutex> lock(mu_); return index_.size(); }
Stats StorageEngine::stats() const { std::shared_lock<std::shared_mutex> lock(mu_); return stats_; }
std::uint64_t StorageEngine::sequence() const { std::shared_lock<std::shared_mutex> lock(mu_); return sequence_; }
}  // namespace minikv
