#include <catch2/catch.hpp>

#include <vr_gameplay_bridge/HiggsSpatialReplayPolicy.h>

#include <limits>
#include <string_view>

namespace Policy = SkyrimTogetherVR::HiggsSpatialReplayPolicy;

namespace
{
[[nodiscard]] Policy::Transform ValidTransform() noexcept
{
    Policy::Transform transform{};
    transform.Translate = {1.0F, 2.0F, 3.0F};
    transform.Scale = 2.0F;
    return transform;
}
} // namespace

TEST_CASE("HIGGS spatial replay requires one exact four-chunk transaction", "[skyrim-vr][higgs]")
{
    Policy::Transaction transaction{};
    const auto transform = ValidTransform();
    const auto chunks = Policy::MakeChunks(transform);

    Policy::Begin(transaction, 42, false);
    REQUIRE(transaction.Active);
    REQUIRE(Policy::Append(transaction, 42, false, 0, chunks[0]) == Policy::AppendResult::Accepted);
    REQUIRE(Policy::Append(transaction, 42, false, 1, chunks[1]) == Policy::AppendResult::Accepted);
    REQUIRE(Policy::Append(transaction, 42, false, 2, chunks[2]) == Policy::AppendResult::Accepted);
    REQUIRE(Policy::Append(transaction, 42, false, 3, chunks[3]) == Policy::AppendResult::Complete);
    REQUIRE(transaction.NextChunk == Policy::kChunkCount);
    REQUIRE(transaction.Relative.Translate.X == 1.0F);
    REQUIRE(transaction.Relative.Translate.Y == 2.0F);
    REQUIRE(transaction.Relative.Translate.Z == 3.0F);
    REQUIRE(transaction.Relative.Scale == 2.0F);
    REQUIRE(Policy::IsSafeTransform(transaction.Relative));
}

TEST_CASE("HIGGS spatial replay rejects duplicate, out-of-order, and incomplete chunks", "[skyrim-vr][higgs]")
{
    Policy::Transaction transaction{};
    const auto chunks = Policy::MakeChunks(ValidTransform());
    Policy::Begin(transaction, 42, false);

    REQUIRE(Policy::Append(transaction, 42, false, 1, chunks[1]) == Policy::AppendResult::Rejected);
    REQUIRE(transaction.NextChunk == 0);
    REQUIRE(Policy::Append(transaction, 42, false, 0, chunks[0]) == Policy::AppendResult::Accepted);
    REQUIRE(Policy::Append(transaction, 42, false, 0, chunks[0]) == Policy::AppendResult::Rejected);
    REQUIRE(Policy::Append(transaction, 42, false, 1, chunks[1]) == Policy::AppendResult::Accepted);
    REQUIRE(Policy::Append(transaction, 42, false, 2, chunks[2]) == Policy::AppendResult::Accepted);
    REQUIRE(transaction.NextChunk == 3);
    REQUIRE_FALSE(transaction.NextChunk == Policy::kChunkCount);
}

TEST_CASE("HIGGS spatial replay rejects invalid transforms and nonfinite chunk lanes", "[skyrim-vr][higgs]")
{
    auto invalidAxes = ValidTransform();
    invalidAxes.Rotate.Rows[1] = invalidAxes.Rotate.Rows[0];
    REQUIRE_FALSE(Policy::IsSafeTransform(invalidAxes));

    auto invalidScale = ValidTransform();
    invalidScale.Scale = 0.0F;
    REQUIRE_FALSE(Policy::IsSafeTransform(invalidScale));

    auto nonfinite = ValidTransform();
    nonfinite.Translate.X = std::numeric_limits<float>::infinity();
    REQUIRE_FALSE(Policy::IsSafeTransform(nonfinite));
    nonfinite = ValidTransform();
    nonfinite.Rotate.Rows[2].Z = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_FALSE(Policy::IsSafeTransform(nonfinite));

    Policy::Transaction transaction{};
    Policy::Begin(transaction, 42, false);
    auto chunk = Policy::MakeChunks(ValidTransform())[0];
    chunk.Lanes[0] = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(Policy::Append(transaction, 42, false, 0, chunk) == Policy::AppendResult::Rejected);
}

TEST_CASE("HIGGS spatial replay isolates left and right hands and sequences", "[skyrim-vr][higgs]")
{
    const auto firstChunk = Policy::MakeChunks(ValidTransform())[0];
    Policy::Transaction left{};
    Policy::Transaction right{};
    Policy::Begin(left, 42, true);
    Policy::Begin(right, 42, false);

    REQUIRE(Policy::Append(left, 42, true, 0, firstChunk) == Policy::AppendResult::Accepted);
    REQUIRE(Policy::Append(right, 42, false, 0, firstChunk) == Policy::AppendResult::Accepted);
    REQUIRE(Policy::Append(left, 42, false, 1, Policy::MakeChunks(ValidTransform())[1]) ==
            Policy::AppendResult::Rejected);
    REQUIRE(Policy::Append(left, 43, true, 1, Policy::MakeChunks(ValidTransform())[1]) ==
            Policy::AppendResult::Rejected);
}

TEST_CASE("HIGGS spatial replay reset, cancel, and drop clear pending chunks", "[skyrim-vr][higgs]")
{
    const auto chunks = Policy::MakeChunks(ValidTransform());
    Policy::Transaction transaction{};
    Policy::Begin(transaction, 42, false);
    REQUIRE(Policy::Append(transaction, 42, false, 0, chunks[0]) == Policy::AppendResult::Accepted);

    Policy::Begin(transaction, 42, false);
    REQUIRE(transaction.NextChunk == 0);
    REQUIRE(transaction.Relative.Scale == 1.0F);
    Policy::Cancel(transaction);
    REQUIRE_FALSE(transaction.Active);
    Policy::Begin(transaction, 42, false);
    Policy::ClearForDrop(transaction);
    REQUIRE_FALSE(transaction.Active);
    REQUIRE(Policy::Append(transaction, 42, false, 0, chunks[0]) == Policy::AppendResult::Rejected);
}

TEST_CASE("HIGGS spatial replay reserves a terminator for grabbed node names", "[skyrim-vr][higgs]")
{
    Policy::Transaction transaction{};
    Policy::Begin(transaction, 42, false, static_cast<std::uint8_t>(Policy::kMaximumNodeBytes));
    REQUIRE(transaction.NodeNameLength == 0);
    REQUIRE(transaction.NodeName.back() == '\0');
}

TEST_CASE("HIGGS spatial replay composes a hand-relative transform into world space", "[skyrim-vr][higgs]")
{
    Policy::Transform hand{};
    hand.Translate = {10.0F, 20.0F, 30.0F};
    hand.Scale = 2.0F;
    hand.Rotate.Rows[0] = {0.0F, -1.0F, 0.0F};
    hand.Rotate.Rows[1] = {1.0F, 0.0F, 0.0F};
    Policy::Transform relative{};
    relative.Translate = {1.0F, -2.0F, 3.0F};
    relative.Scale = 0.5F;

    const auto world = Policy::ComposeHandRelative(hand, relative);
    REQUIRE(world.Translate.X == 14.0F);
    REQUIRE(world.Translate.Y == 22.0F);
    REQUIRE(world.Translate.Z == 36.0F);
    REQUIRE(world.Scale == 1.0F);
    REQUIRE(world.Rotate.Rows[0].Y == -1.0F);
    REQUIRE(world.Rotate.Rows[1].X == 1.0F);
    REQUIRE(world.Rotate.Rows[2].Z == 1.0F);
}

TEST_CASE("HIGGS spatial replay solves the object root from a named grabbed node", "[skyrim-vr][higgs]")
{
    Policy::Transform currentRoot{};
    currentRoot.Translate = {100.0F, 200.0F, 300.0F};

    Policy::Transform currentGrabbedNode{};
    currentGrabbedNode.Translate = {110.0F, 200.0F, 300.0F};

    Policy::Transform desiredGrabbedNode{};
    desiredGrabbedNode.Translate = {510.0F, 600.0F, 700.0F};

    const auto desiredRoot = Policy::SolveObjectRootWorld(
        currentGrabbedNode, currentRoot, desiredGrabbedNode);
    REQUIRE(desiredRoot.Translate.X == 500.0F);
    REQUIRE(desiredRoot.Translate.Y == 600.0F);
    REQUIRE(desiredRoot.Translate.Z == 700.0F);
    REQUIRE(Policy::IsSafeTransform(desiredRoot));
}

TEST_CASE("HIGGS spatial replay validates flags for ordered begin and chunk records", "[skyrim-vr][higgs]")
{
    REQUIRE(Policy::IsSpatialBegin(Policy::kHasHand | Policy::kSpatialBegin));
    REQUIRE_FALSE(Policy::IsSpatialBegin(Policy::kHasHand | Policy::kSpatialBegin | (1u << Policy::kChunkIndexShift)));
    REQUIRE(Policy::IsSpatialChunk(Policy::kHasHand | Policy::kSpatialChunk | (2u << Policy::kChunkIndexShift)));
    REQUIRE_FALSE(Policy::IsSpatialChunk(Policy::kHasHand | Policy::kSpatialBegin | Policy::kSpatialChunk));
}

TEST_CASE("HIGGS spatial replay keeps a bounded grabbed-node identity before transform chunks", "[skyrim-vr][higgs]")
{
    Policy::Transaction transaction{};
    Policy::Begin(transaction, 9, true, 5);
    REQUIRE(transaction.Active);

    std::array<char, Policy::kNodeBytesPerChunk> node{};
    node[0] = 'N';
    node[1] = 'o';
    node[2] = 'd';
    node[3] = 'e';
    node[4] = 'X';
    REQUIRE(Policy::AppendNode(transaction, 9, true, 0, node));
    CHECK(std::string_view(transaction.NodeName.data(), transaction.NodeNameLength) == "NodeX");

    const auto chunks = Policy::MakeChunks(Policy::Transform{});
    for (std::size_t index = 0; index < chunks.size(); ++index)
        CHECK(Policy::Append(transaction, 9, true, static_cast<std::uint32_t>(index), chunks[index]) !=
              Policy::AppendResult::Rejected);
}
