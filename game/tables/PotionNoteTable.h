#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace OpenYAMM::Game
{
class PotionNoteTable
{
public:
    struct Entry
    {
        uint32_t targetItemId = 0;
        uint32_t heldItemId = 0;
        uint32_t autonoteId = 0;
    };

    bool loadFromRows(const std::vector<std::vector<std::string>> &rows);

    std::optional<uint32_t> autonoteIdForMix(uint32_t targetItemId, uint32_t heldItemId) const;
    size_t entryCount() const;
    // Every mix that records a potion note, in table order.
    const std::vector<Entry> &entries() const;

private:
    static uint64_t key(uint32_t rowItemId, uint32_t columnItemId);

    std::unordered_map<uint64_t, uint32_t> m_autonoteIds;
    std::vector<Entry> m_entries;
};
}
