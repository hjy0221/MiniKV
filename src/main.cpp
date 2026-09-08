#include "minikv/engine.hpp"
#include <iostream>
#include <sstream>

int main(int argc, char** argv) {
  const std::string path = argc > 1 ? argv[1] : "minikv.log";
  try {
    minikv::StorageEngine db(path);
    std::cout << "MiniKV ready (PUT key value | GET key | DELETE key | STATS | EXIT)\n";
    std::string line;
    while (std::cout << "> " && std::getline(std::cin, line)) {
      std::istringstream input(line); std::string command, key; input >> command;
      if (command == "PUT") {
        input >> key; std::string value; std::getline(input >> std::ws, value);
        if (key.empty() || value.empty()) { std::cout << "ERR usage: PUT key value\n"; continue; }
        db.put(key, value); std::cout << "OK\n";
      } else if (command == "GET") {
        input >> key; auto value = db.get(key); std::cout << (value ? *value : "(nil)") << '\n';
      } else if (command == "DELETE") {
        input >> key; std::cout << (db.erase(key) ? "1" : "0") << '\n';
      } else if (command == "STATS") {
        auto s = db.stats(); std::cout << "keys=" << db.size() << " seq=" << db.sequence()
          << " puts=" << s.puts << " gets=" << s.gets << " deletes=" << s.deletes
          << " cache_hits=" << s.cache_hits << " cache_misses=" << s.cache_misses
          << " recovered=" << s.recovered_records << '\n';
      } else if (command == "EXIT" || command == "QUIT") break;
      else if (!command.empty()) std::cout << "ERR unknown command\n";
    }
  } catch (const std::exception& e) { std::cerr << "fatal: " << e.what() << '\n'; return 1; }
}

