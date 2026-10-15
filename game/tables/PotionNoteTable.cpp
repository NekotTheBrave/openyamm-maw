#include "game/tables/PotionNoteTable.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <utility>

namespace OpenYAMM::Game
{
namespace
{
constexpr uint32_t PotionMatrixHeaderItemId = 220;
constexpr size_t PotionMatrixFirstColumn = 7;

std::string trimCopy(const std::string &text)
{
    size_t begin = 0;

    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])))
    {
        ++begin;
    }

    size_t end = text.size();

    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
    {
        --end;
    }

    return text.substr(begin, end - begin);
}

bool parseUnsigned(const std::string &text, uint32_t &value)
{
    const std::string trimmed = trimCopy(text);

    if (trimmed.empty())
    {
        return false;
    }

    for (char character : trimmed)
    {
        if (!std::isdigit(static_cast<unsigned char>(character)))
        {
            return false;
        }
    }

    value = static_cast<uint32_t>(std::strtoul(trimmed.c_str(), nullptr, 10));
    return true;
}

bool isNoNoteCell(const std::string &cell)
{
    return cell.empty()
        || cell == "0"
        || cell == "no"
        || cell == "No"
        || cell == "NO";
}

std::vector<std::pair<size_t, uint32_t>> matrixTargetColumnsFromRows(
    const std::vector<std::vector<std::string>> &rows)
{
    for (const std::vector<std::string> &row : rows)
    {
        uint32_t rowItemId = 0;

        if (!row.empty() && parseUnsigned(row[0], rowItemId) && rowItemId == PotionMatrixHeaderItemId)
        {
            std::vector<std::pair<size_t, uint32_t>> targetColumns;

            for (size_t columnIndex = PotionMatrixFirstColumn; columnIndex < row.size(); ++columnIndex)
            {
                uint32_t targetItemId = 0;

                if (parseUnsigned(row[columnIndex], targetItemId))
                {
                    targetColumns.push_back({columnIndex, targetItemId});
                }
            }

            return targetColumns;
        }
    }

    return {};
}
}

bool PotionNoteTable::loadFromRows(const std::vector<std::vector<std::string>> &rows)
{
    m_autonoteIds.clear();
    m_entries.clear();

    const std::vector<std::pair<size_t, uint32_t>> targetColumns = matrixTargetColumnsFromRows(rows);

    if (targetColumns.empty())
    {
        return false;
    }

    std::vector<uint32_t> targetItemIds;
    targetItemIds.reserve(targetColumns.size());

    for (const std::pair<size_t, uint32_t> &targetColumn : targetColumns)
    {
        targetItemIds.push_back(targetColumn.second);
    }

    const size_t maxMatrixColumnIndex = targetColumns.back().first;

    for (size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex)
    {
        std::vector<std::string> row = rows[rowIndex];
        uint32_t rowItemId = 0;

        if (row.empty()
            || !parseUnsigned(row[0], rowItemId)
            || std::find(targetItemIds.begin(), targetItemIds.end(), rowItemId) == targetItemIds.end())
        {
            continue;
        }

        while (row.size() <= maxMatrixColumnIndex
            && rowIndex + 1 < rows.size()
            && !rows[rowIndex + 1].empty()
            && !parseUnsigned(rows[rowIndex + 1][0], rowItemId))
        {
            const std::vector<std::string> &continuationRow = rows[rowIndex + 1];

            for (size_t columnIndex = 1; columnIndex < continuationRow.size(); ++columnIndex)
            {
                row.push_back(continuationRow[columnIndex]);
            }

            ++rowIndex;
        }

        for (const std::pair<size_t, uint32_t> &targetColumn : targetColumns)
        {
            const size_t columnIndex = targetColumn.first;
            const uint32_t targetItemId = targetColumn.second;

            if (columnIndex >= row.size())
            {
                return false;
            }

            const std::string cell = trimCopy(row[columnIndex]);

            if (isNoNoteCell(cell))
            {
                continue;
            }

            uint32_t autonoteId = 0;

            if (!parseUnsigned(cell, autonoteId))
            {
                return false;
            }

            if (autonoteId != 0 && m_autonoteIds.insert_or_assign(key(rowItemId, targetItemId), autonoteId).second)
            {
                m_entries.push_back({rowItemId, targetItemId, autonoteId});
            }
        }
    }

    return !m_autonoteIds.empty();
}

std::optional<uint32_t> PotionNoteTable::autonoteIdForMix(uint32_t targetItemId, uint32_t heldItemId) const
{
    const std::unordered_map<uint64_t, uint32_t>::const_iterator found =
        m_autonoteIds.find(key(targetItemId, heldItemId));
    return found != m_autonoteIds.end()
        ? std::optional<uint32_t>(found->second)
        : std::nullopt;
}

size_t PotionNoteTable::entryCount() const
{
    return m_autonoteIds.size();
}

const std::vector<PotionNoteTable::Entry> &PotionNoteTable::entries() const
{
    return m_entries;
}

uint64_t PotionNoteTable::key(uint32_t rowItemId, uint32_t columnItemId)
{
    return (static_cast<uint64_t>(rowItemId) << 32) | static_cast<uint64_t>(columnItemId);
}
}
