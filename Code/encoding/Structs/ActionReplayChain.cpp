#include <Structs/ActionReplayChain.h>

#include <algorithm>

using TiltedPhoques::Serialization;

bool ActionReplayChain::operator==(const ActionReplayChain& acRhs) const noexcept
{
    return ResetAnimationGraph == acRhs.ResetAnimationGraph && Actions == acRhs.Actions;
}

bool ActionReplayChain::operator!=(const ActionReplayChain& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void ActionReplayChain::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteBool(aWriter, ResetAnimationGraph);
    const auto count = std::min(Actions.size(), kMaximumActions);
    aWriter.WriteBits(count, 8);
    for (size_t i = 0; i < count; ++i)
    {
        Actions[i].GenerateDifferential(ActionEvent{}, aWriter);
    }
}

void ActionReplayChain::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    *this = {};
    ResetAnimationGraph = Serialization::ReadBool(aReader);
    uint64_t actionsCount = 0;
    if (!aReader.ReadBits(actionsCount, 8))
    {
        IsDecodedValid = false;
        return;
    }
    try
    {
        Actions.resize(static_cast<std::size_t>(actionsCount));
        for (ActionEvent& replayAction : Actions)
        {
            replayAction.ApplyDifferential(aReader);
            if (!replayAction.IsDecodedValid)
            {
                Actions.clear();
                IsDecodedValid = false;
                return;
            }
        }
    }
    catch (...)
    {
        Actions.clear();
        IsDecodedValid = false;
    }
}
