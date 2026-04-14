#pragma once

#include "board.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>

// ====================== BOT INTERFACE ======================

class Bot {
public:
    virtual ~Bot() = default;
    virtual Move get_move(const PlayerBoard& board) = 0;
    virtual void reset() {}
};

// ====================== REGISTRY ======================

struct BotEntry {
    std::string name;
    std::string description;
    std::function<std::unique_ptr<Bot>()> factory;
};

class BotRegistry {
public:
    static BotRegistry& instance();

    void register_bot(const std::string& name,
                      const std::string& description,
                      std::function<std::unique_ptr<Bot>()> factory);

    std::unique_ptr<Bot> create(const std::string& name) const;
    const std::vector<BotEntry>& list() const;
    bool has(const std::string& name) const;

private:
    BotRegistry() = default;
    std::vector<BotEntry> entries_;
    std::unordered_map<std::string, size_t> name_index_;
};
