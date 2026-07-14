#pragma once

#include <Events/EventDispatcher.h>
#include <Games/Events.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

struct TESLoadGameEvent;
struct World;

struct VRLifecycleService final : BSTEventSink<TESLoadGameEvent>
{
    enum class State
    {
        Boot,
        Suspended,
        Stabilizing,
        Ready,
        Teardown,
    };

    explicit VRLifecycleService(World& aWorld) noexcept;
    ~VRLifecycleService() noexcept = default;

    TP_NOCOPYMOVE(VRLifecycleService);

    void Update(double aDelta) noexcept;
    void BeginTeardown() noexcept;

    [[nodiscard]] bool IsReady() const noexcept { return m_state == State::Ready; }
    [[nodiscard]] State GetState() const noexcept { return m_state; }
    [[nodiscard]] const char* GetStateName() const noexcept;
    [[nodiscard]] uint64_t GetEpoch() const noexcept { return m_epoch; }
    [[nodiscard]] uint32_t GetOwnerThreadId() const noexcept { return m_ownerThreadId; }
    [[nodiscard]] uint32_t GetPlayerFormId() const noexcept { return m_readySample.PlayerFormId; }
    [[nodiscard]] uint32_t GetPlayerCellFormId() const noexcept { return m_readySample.CellFormId; }

private:
    struct Sample
    {
        std::uintptr_t Player{};
        std::uintptr_t Base{};
        std::uintptr_t Cell{};
        uint32_t PlayerFormId{};
        uint32_t BaseFormId{};
        uint32_t CellFormId{};

        bool operator==(const Sample&) const noexcept = default;
    };

    BSTEventResult OnEvent(const TESLoadGameEvent*, const EventDispatcher<TESLoadGameEvent>*) override;
    void Suspend(const char* apReason, bool aAdvanceEpoch = false) noexcept;
    void StartStabilizing(const Sample& acSample) noexcept;
    void WriteStatusFile() noexcept;

    World& m_world;
    std::filesystem::path m_statusPath;
    std::atomic_bool m_loadInvalidated{false};
    State m_state{State::Boot};
    Sample m_candidateSample{};
    Sample m_readySample{};
    std::chrono::steady_clock::time_point m_candidateSince{};
    std::string m_suspendReason{"boot"};
    uint64_t m_epoch{1};
    uint32_t m_ownerThreadId{0};
    uint32_t m_stableTickCount{0};
    double m_statusTimer{0.0};
    bool m_statusDirty{true};
};
