#include "game/mod/ModHooks.h"
#include "game/gameplay/JournalQuestRuntime.h"

#include "game/party/Party.h"
#include "game/tables/JournalQuestTable.h"

#include <map>

namespace OpenYAMM::Game
{
std::vector<std::string> buildCurrentQuestTexts(const JournalQuestTable &questTable, const Party &party)
{
    std::vector<std::string> questTexts;
    // A mod may write a bit's text (Game.QuestsTxt), the bits the table leaves empty included.
    std::map<uint32_t, std::string> textsByBit;

    for (const JournalQuestEntry &entry : questTable.entries())
    {
        if (questTable.isVisible(entry, party))
        {
            const std::string *pModText = modQuestText(entry.qbitId);
            textsByBit[entry.qbitId] = pModText != nullptr ? *pModText : entry.text;
        }
    }

    for (uint32_t qbitId : modQuestTextBits())
    {
        if (!questTable.hasQuestText(qbitId) && party.hasQuestBit(qbitId))
        {
            textsByBit[qbitId] = *modQuestText(qbitId);
        }
    }

    for (auto &[qbitId, text] : textsByBit)
    {
        (void)qbitId;

        if (!text.empty() && text != "0")
        {
            questTexts.push_back(std::move(text));
        }
    }

    for (std::string &text : modQuestLogTexts())
    {
        questTexts.push_back(std::move(text));
    }

    return questTexts;
}
} // namespace OpenYAMM::Game
