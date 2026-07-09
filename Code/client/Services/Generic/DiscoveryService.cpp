#include <TiltedOnlinePCH.h>

#include <Services/DiscoveryService.h>
#include <Games/TES.h>

#include <Games/References.h>

#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <Forms/TESNPC.h>
#include <Forms/TESForm.h>

#include <Events/ActorAddedEvent.h>
#include <Events/ActorRemovedEvent.h>
#include <Events/PreUpdateEvent.h>
#include <Events/GridCellChangeEvent.h>
#include <Events/CellChangeEvent.h>
#include <Events/LocationChangeEvent.h>
#include <Events/ConnectedEvent.h>
#include <Events/ConnectionErrorEvent.h>

#include <World.h>
#include <vr_common/VRHandoffPath.h>

#include <fstream>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY
#define TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_DISCOVERY_SERVICE
#define TP_SKYRIM_VR_ENABLE_DISCOVERY_SERVICE 0
#endif

namespace
{
constexpr double kVrDiscoveryStatusWriteInterval = 1.0;
constexpr uint32_t kVrDiscoveryStatusMaxActors = 64;
constexpr char kVrDiscoveryStatusFileName[] = "SkyrimTogetherVR.discovery";

constexpr bool kVrDiscoveryStatusEnabled = TP_SKYRIM_VR && TP_SKYRIM_VR_ENABLE_DISCOVERY_SERVICE;
constexpr bool kVrSkipStrictConnectionEnforcement = TP_SKYRIM_VR && TP_SKYRIM_VR_ENABLE_DISCOVERY_SERVICE;

std::filesystem::path GetHandoffDirectory()
{
    return SkyrimTogetherVR::Handoff::GetDirectory();
}
} // namespace

DiscoveryService::DiscoveryService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
{
    if constexpr (kVrDiscoveryStatusEnabled)
    {
        const auto handoffDir = GetHandoffDirectory();
        std::error_code ec;
        std::filesystem::create_directories(handoffDir, ec);
        m_vrDiscoveryStatusPath = handoffDir / kVrDiscoveryStatusFileName;
        spdlog::info("SkyrimTogetherVR discovery handoff status file: {}", m_vrDiscoveryStatusPath.string());
    }

    m_preUpdateConnection = m_dispatcher.sink<PreUpdateEvent>().connect<&DiscoveryService::OnUpdate>(this);
    m_connectedConnection = m_dispatcher.sink<ConnectedEvent>().connect<&DiscoveryService::OnConnected>(this);

    auto* pEventDispatcherManager = EventDispatcherManager::Get();
    if (pEventDispatcherManager)
        pEventDispatcherManager->GetLoadGameEventData().RegisterSink(this);
    else
        spdlog::warn("DiscoveryService could not register TESLoadGameEvent sink; dispatcher manager is unavailable");
}

void DiscoveryService::VisitCell(bool aForceTrigger) noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;

    if (pPlayer->GetWorldSpace())
        VisitExteriorCell(aForceTrigger);
    else if (pPlayer->GetParentCellEx())
        VisitInteriorCell(aForceTrigger);

    TESForm* pLocation = pPlayer->GetCurrentLocation();
    // exactly how the game does it too
    if (m_pLocation != pLocation)
    {
        m_dispatcher.trigger(LocationChangeEvent());
        m_pLocation = pLocation;
        m_vrDiscoveryStatusDirty = true;
    }
}

void DiscoveryService::VisitExteriorCell(bool aForceTrigger) noexcept
{
    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;

    const auto pWorldSpace = pPlayer->GetWorldSpace();
    if (!pWorldSpace)
        return;

    m_interiorCellId = 0;

    const TES* pTES = TES::Get();
    if (!pTES)
        return;

    auto* pModManager = ModManager::Get();
    if (!pModManager)
        return;

    const uint32_t worldSpaceId = pWorldSpace->GetFormIdData();
    const GridCellCoords gameCurrentGrid(pTES->GetCurrentGridXData(), pTES->GetCurrentGridYData());
    const GridCellCoords gameCenterGrid(pTES->GetCenterGridXData(), pTES->GetCenterGridYData());

    if (m_worldSpaceId != worldSpaceId || aForceTrigger)
    {
        DetectGridCellChange(pWorldSpace, true);
        // If the world space changes, then we want to send out a CellChangeEvent out too.
        aForceTrigger = true;
    }
    else if (gameCenterGrid != m_centerGrid)
    {
        DetectGridCellChange(pWorldSpace, false);
    }

    if (gameCurrentGrid != m_currentGrid || aForceTrigger)
    {
        CellChangeEvent cellChangeEvent{};

        if (!m_world.GetModSystem().GetServerModId(worldSpaceId, cellChangeEvent.WorldSpaceId))
        {
            spdlog::error("Failed to find world space id for form id {:X}", worldSpaceId);
            return;
        }

        TESObjectCELL* pCell = pPlayer->GetParentCellEx();
        if (!pCell)
            pCell = pModManager->GetCellFromCoordinates(gameCurrentGrid.X, gameCurrentGrid.Y, pWorldSpace, false);
        if (!pCell)
        {
            spdlog::warn("Failed to resolve current exterior cell at ({}, {}) in worldspace {:X}", gameCurrentGrid.X, gameCurrentGrid.Y, worldSpaceId);
            return;
        }

        const uint32_t cellFormId = pCell->GetFormIdData();
        if (!m_world.GetModSystem().GetServerModId(cellFormId, cellChangeEvent.CellId))
        {
            spdlog::error("Failed to find cell id for form id {:X}", cellFormId);
            return;
        }

        cellChangeEvent.CurrentCoords = gameCurrentGrid;

        m_dispatcher.trigger(cellChangeEvent);

        m_currentGrid = gameCurrentGrid;
        m_vrDiscoveryStatusDirty = true;
    }
}

void DiscoveryService::VisitInteriorCell(bool aForceTrigger) noexcept
{
    ResetCachedCellData();

    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    const TESObjectCELL* pParentCell = pPlayer ? pPlayer->GetParentCellEx() : nullptr;
    if (!pParentCell)
        return;

    const uint32_t cellId = pParentCell->GetFormIdData();
    if (m_interiorCellId != cellId || aForceTrigger)
    {
        CellChangeEvent cellChangeEvent{};

        if (!m_world.GetModSystem().GetServerModId(cellId, cellChangeEvent.CellId))
        {
            spdlog::error("Failed to find cell id {:X}", cellId);
            return;
        }

        m_dispatcher.trigger(cellChangeEvent);
        m_interiorCellId = cellId;
        m_vrDiscoveryStatusDirty = true;
    }
}

void DiscoveryService::DetectGridCellChange(TESWorldSpace* aWorldSpace, bool aNewCellGrid) noexcept
{
    if (!aWorldSpace)
        return;

    GridCellChangeEvent changeEvent{};

    const uint32_t worldSpaceId = aWorldSpace->GetFormIdData();
    changeEvent.WorldSpaceId = worldSpaceId;

    const TES* pTES = TES::Get();
    if (!pTES)
        return;

    auto* pModManager = ModManager::Get();
    if (!pModManager)
        return;

    const int32_t startGridX = pTES->GetCenterGridXData() - GridCellCoords::m_gridsToLoad / 2;
    const int32_t startGridY = pTES->GetCenterGridYData() - GridCellCoords::m_gridsToLoad / 2;

    for (int32_t i = 0; i < GridCellCoords::m_gridsToLoad; ++i)
    {
        for (int32_t j = 0; j < GridCellCoords::m_gridsToLoad; ++j)
        {
            // If it is a new cell grid, don't check for previously loaded cells.
            if (!aNewCellGrid)
            {
                if (GridCellCoords::IsCellInGridCell(m_centerGrid, {startGridX + i, startGridY + j}, false))
                    continue;
            }

            const TESObjectCELL* pCell = pModManager->GetCellFromCoordinates(startGridX + i, startGridY + j, aWorldSpace, 0);

            if (!pCell)
            {
                spdlog::warn("Cell not found at coordinates ({}, {}) in worldspace {:X}", startGridX + i, startGridY + j, worldSpaceId);
                continue;
            }

            GameId cellId{};
            const uint32_t cellFormId = pCell->GetFormIdData();
            if (!m_world.GetModSystem().GetServerModId(cellFormId, cellId))
            {
                spdlog::error("Failed to find cell id for form id {:X}", cellFormId);
                continue;
            }

            changeEvent.Cells.push_back(cellId);
        }
    }

    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    TESObjectCELL* pCell = pPlayer ? pPlayer->GetParentCellEx() : nullptr;
    if (!pCell)
        pCell = pModManager->GetCellFromCoordinates(pTES->GetCurrentGridXData(), pTES->GetCurrentGridYData(), aWorldSpace, false);
    if (!pCell)
    {
        spdlog::warn("Failed to resolve player exterior cell at ({}, {}) in worldspace {:X}", pTES->GetCurrentGridXData(), pTES->GetCurrentGridYData(), worldSpaceId);
        return;
    }

    const uint32_t playerCellFormId = pCell->GetFormIdData();
    if (!m_world.GetModSystem().GetServerModId(playerCellFormId, changeEvent.PlayerCell))
    {
        spdlog::error("Failed to find cell id for form id {:X}", playerCellFormId);
        return;
    }

    changeEvent.CenterCoords = m_centerGrid = {pTES->GetCenterGridXData(), pTES->GetCenterGridYData()};

    m_dispatcher.trigger(changeEvent);

    m_worldSpaceId = worldSpaceId;
    m_vrDiscoveryStatusDirty = true;
}

void DiscoveryService::VisitForms() noexcept
{
    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;

    static Set<uint32_t> s_previousForms;
    s_previousForms = m_forms;

    const auto visitor = [this](TESObjectREFR* apReference)
    {
        if (!apReference)
            return;

        const auto formId = apReference->GetFormIdData();

        if (!m_forms.count(formId))
        {
            m_forms.insert(formId);

            m_dispatcher.enqueue(ActorAddedEvent(formId));
            m_vrDiscoveryStatusDirty = true;
        }
        else
            s_previousForms.erase(formId);
    };

    ProcessLists* const pProcessLists = ProcessLists::Get();
    if (!pProcessLists)
        return;

    for (uint32_t i = 0; i < pProcessLists->highActorHandleArray.length; ++i)
    {
        TESObjectREFR* const pRefr = TESObjectREFR::GetByHandle(pProcessLists->highActorHandleArray[i]);
        if (pRefr)
        {
            if (pRefr->GetNiNode())
            {
                visitor(pRefr);
            }
        }
    }

    // Not in actor holder
    visitor(pPlayer);

    // We dispatch removal events first to prevent needless reallocations
    for (uint32_t formId : s_previousForms)
    {
        m_dispatcher.trigger(ActorRemovedEvent(formId));
        m_forms.erase(formId);
        m_vrDiscoveryStatusDirty = true;
    }

    // Dispatch all adds
    m_dispatcher.update<ActorAddedEvent>();
}

void DiscoveryService::OnUpdate(const PreUpdateEvent& acUpdateEvent) noexcept
{
    VisitCell();
    VisitForms();

    if constexpr (kVrDiscoveryStatusEnabled)
    {
        m_vrDiscoveryStatusTimer += acUpdateEvent.Delta;
        if (m_vrDiscoveryStatusDirty || m_vrDiscoveryStatusTimer >= kVrDiscoveryStatusWriteInterval)
        {
            m_vrDiscoveryStatusTimer = 0.0;
            WriteVrDiscoveryStatusFile();
        }
    }
    else
    {
        TP_UNUSED(acUpdateEvent);
    }
}

void DiscoveryService::OnConnected(const ConnectedEvent& acEvent) noexcept
{
    if constexpr (kVrSkipStrictConnectionEnforcement)
    {
        TP_UNUSED(acEvent);
        VisitCell(true);
        WriteVrDiscoveryStatusFile();
        return;
    }

    // uGridsToLoad should always be 5, as this is what the server enforces
    auto* pSetting = INISettingCollection::Get()->GetSetting("uGridsToLoad:General");
    if (pSetting && pSetting->data != 5)
    {
        ConnectionErrorEvent errorEvent{};
        errorEvent.ErrorDetail = "{\"error\": \"bad_uGridsToLoad\"}";

        m_world.GetRunner().Trigger(errorEvent);
    }

    VisitCell(true);
}

BSTEventResult DiscoveryService::OnEvent(const TESLoadGameEvent*, const EventDispatcher<TESLoadGameEvent>*)
{
    spdlog::info("Finished loading, triggering visit cell");

    if constexpr (!kVrSkipStrictConnectionEnforcement)
    {
        const TiltedPhoques::String defaultModlist[7] = {"Skyrim.esm",     "Update.esm",        "Dawnguard.esm",     "HearthFires.esm",
                                                         "Dragonborn.esm", "_ResourcePack.esl", "SkyrimTogether.esp"};

        auto& currentModlist = ModManager::Get()->mods;

        bool isModlistEqual = currentModlist.Size() == 7;

        if (isModlistEqual)
        {
            int i = 0;
            for (const auto& currentMod : currentModlist)
            {
                if (currentMod->filename != defaultModlist[i])
                {
                    isModlistEqual = false;
                    break;
                }

                i++;
            }
        }

        if (!isModlistEqual)
        {
            ConnectionErrorEvent errorEvent{};
            errorEvent.ErrorDetail = "{\"error\": \"non_default_install\"}";

            m_world.GetRunner().Trigger(errorEvent);
        }
    }
    else
    {
        spdlog::info("SkyrimTogetherVR discovery VR mode: skipping default-modlist enforcement");
    }

    VisitCell(true);
    WriteVrDiscoveryStatusFile();

    return BSTEventResult::kOk;
}

void DiscoveryService::WriteVrDiscoveryStatusFile() noexcept
{
    if constexpr (!kVrDiscoveryStatusEnabled)
        return;

    if (m_vrDiscoveryStatusPath.empty())
        return;

    std::error_code ec;
    std::filesystem::create_directories(m_vrDiscoveryStatusPath.parent_path(), ec);

    std::ofstream file(m_vrDiscoveryStatusPath, std::ios::trunc);
    if (!file)
        return;

    const auto* pPlayer = PlayerCharacter::Get();
    const auto* pCell = pPlayer ? pPlayer->GetParentCellEx() : nullptr;
    const auto* pWorldSpace = pPlayer ? pPlayer->GetWorldSpace() : nullptr;

    file << "ready=" << (pPlayer && pCell ? "1" : "0") << "\n";
    file << "actorCount=" << m_forms.size() << "\n";
    file << "actorLimit=" << kVrDiscoveryStatusMaxActors << "\n";
    file << "currentGrid=" << m_currentGrid.X << "," << m_currentGrid.Y << "\n";
    file << "centerGrid=" << m_centerGrid.X << "," << m_centerGrid.Y << "\n";
    file << "cachedWorldSpaceFormId=" << m_worldSpaceId << "\n";
    file << "cachedInteriorCellFormId=" << m_interiorCellId << "\n";
    file << "playerCellFormId=" << (pCell ? pCell->GetFormIdData() : 0) << "\n";
    file << "playerWorldSpaceFormId=" << (pWorldSpace ? pWorldSpace->GetFormIdData() : 0) << "\n";
    file << "locationFormId=" << (m_pLocation ? m_pLocation->GetFormIdData() : 0) << "\n";

    uint32_t actorIndex = 0;
    for (uint32_t formId : m_forms)
    {
        if (actorIndex >= kVrDiscoveryStatusMaxActors)
            break;

        file << "actor." << actorIndex << ".formId=" << formId << "\n";
        ++actorIndex;
    }

    m_vrDiscoveryStatusDirty = false;
}

void DiscoveryService::ResetCachedCellData() noexcept
{
    m_worldSpaceId = 0;
    m_centerGrid.Reset();
    m_currentGrid.Reset();
}
