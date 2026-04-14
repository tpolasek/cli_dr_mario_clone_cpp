#include "bot_registry.h"

BotRegistry& BotRegistry::instance() {
    static BotRegistry reg;
    return reg;
}

void BotRegistry::register_bot(const std::string& name,
                               const std::string& description,
                               std::function<std::unique_ptr<Bot>()> factory) {
    auto it = name_index_.find(name);
    if (it != name_index_.end()) {
        entries_[it->second].description = description;
        entries_[it->second].factory = std::move(factory);
        return;
    }
    name_index_[name] = entries_.size();
    entries_.push_back({name, description, std::move(factory)});
}

std::unique_ptr<Bot> BotRegistry::create(const std::string& name) const {
    auto it = name_index_.find(name);
    if (it != name_index_.end()) return entries_[it->second].factory();
    return nullptr;
}

const std::vector<BotEntry>& BotRegistry::list() const {
    return entries_;
}

bool BotRegistry::has(const std::string& name) const {
    return name_index_.count(name) > 0;
}
