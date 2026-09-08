#pragma once
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace minikv {
class LruCache {
 public:
  explicit LruCache(std::size_t capacity) : capacity_(capacity) {}

  std::optional<std::string> get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = entries_.find(key);
    if (it == entries_.end()) return std::nullopt;
    order_.splice(order_.begin(), order_, it->second.second);
    return it->second.first;
  }

  void put(const std::string& key, const std::string& value) {
    if (capacity_ == 0) return;
    std::lock_guard<std::mutex> lock(mu_);
    auto it = entries_.find(key);
    if (it != entries_.end()) {
      it->second.first = value;
      order_.splice(order_.begin(), order_, it->second.second);
      return;
    }
    order_.push_front(key);
    entries_[key] = {value, order_.begin()};
    if (entries_.size() > capacity_) {
      entries_.erase(order_.back());
      order_.pop_back();
    }
  }

  void erase(const std::string& key) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = entries_.find(key);
    if (it == entries_.end()) return;
    order_.erase(it->second.second);
    entries_.erase(it);
  }

 private:
  std::size_t capacity_;
  std::mutex mu_;
  std::list<std::string> order_;
  std::unordered_map<std::string,
      std::pair<std::string, std::list<std::string>::iterator>> entries_;
};
}  // namespace minikv

