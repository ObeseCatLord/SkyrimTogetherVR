#include <Services/ObjectService.h>

#include <World.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Events/CellChangeEvent.h>
#include <Events/ActivateEvent.h>
#include <Events/LockChangeEvent.h>
#include <Events/ScriptAnimationEvent.h>
#include <Messages/ServerTimeSettings.h>
#include <Messages/AssignObjectsRequest.h>
#include <Messages/AssignObjectsResponse.h>
#include <Messages/ActivateRequest.h>
#include <Messages/NotifyActivate.h>
#include <Messages/LockChangeRequest.h>
#include <Messages/NotifyLockChange.h>
#include <Messages/ScriptAnimationRequest.h>
#include <Messages/NotifyScriptAnimation.h>

#include <PlayerCharacter.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <Forms/BGSEncounterZone.h>

#include <algorithm>
#include <array>
#include <inttypes.h>

namespace
{
void LogObjectServiceFailure(const char* apMessage) noexcept
{
    try
    {
        spdlog::error("{}", apMessage);
    }
    catch (...)
    {
    }
}

struct ObjectResponseIdentity
{
    uint32_t ServerId{};
    GameId FormId{};
};
} // namespace

ObjectService::ObjectService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport)
    : m_world(aWorld)
    , m_transport(aTransport)
{
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&ObjectService::OnDisconnected>(this);
    m_cellChangeConnection = aDispatcher.sink<CellChangeEvent>().connect<&ObjectService::OnCellChange>(this);
    m_onActivateConnection = aDispatcher.sink<ActivateEvent>().connect<&ObjectService::OnActivate>(this);
    m_activateConnection = aDispatcher.sink<NotifyActivate>().connect<&ObjectService::OnActivateNotify>(this);
    m_lockChangeConnection = aDispatcher.sink<LockChangeEvent>().connect<&ObjectService::OnLockChange>(this);
    m_lockChangeNotifyConnection = aDispatcher.sink<NotifyLockChange>().connect<&ObjectService::OnLockChangeNotify>(this);
    m_assignObjectConnection = aDispatcher.sink<AssignObjectsResponse>().connect<&ObjectService::OnAssignObjectsResponse>(this);
    m_scriptAnimationConnection = aDispatcher.sink<ScriptAnimationEvent>().connect<&ObjectService::OnScriptAnimationEvent>(this);
    m_scriptAnimationNotifyConnection = aDispatcher.sink<NotifyScriptAnimation>().connect<&ObjectService::OnNotifyScriptAnimation>(this);

    if (auto* pEventDispatcherManager = EventDispatcherManager::Get())
        pEventDispatcherManager->GetActivateEventData().RegisterSink(this);
}

bool IsPlayerHome(const TESObjectCELL* pCell) noexcept
{
    const auto* pLoadedCellData = pCell ? pCell->GetLoadedCellData() : nullptr;
    const auto* pEncounterZone = pLoadedCellData ? pLoadedCellData->GetEncounterZoneData() : nullptr;
    if (pEncounterZone)
    {
        // Only return true if cell has the NoResetZone encounter zone
        if (pEncounterZone->GetFormIdData() == 0xf90b1)
        {
            switch (pCell->GetFormIdData())
            {
            case 0xeec55: // one known exception: Sinderion's Field Lab
                return false;
            default: return true;
            }
        }
    }

    return false;
}

bool ShouldSyncObject(const TESObjectREFR* apObject) noexcept
{
    if (!apObject)
        return false;

    switch (apObject->GetFormIdData())
    {
    case 0x39CF1: // Don't sync the chest in the "Diplomatic Immunity" quest
        return false;
    case 0x3EF03: // ...as well as in the "No One Escapes Cidhna Mine" quest
        return false;
    default: return true;
    }
}

void ObjectService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    // TODO(cosideci): clear object components
}

void ObjectService::OnCellChange(const CellChangeEvent& acEvent) noexcept try
{
    if (!m_transport.IsConnected())
        return;

    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    TESObjectCELL* pCell = pPlayer ? pPlayer->GetParentCellData() : nullptr;
    if (!pCell)
        return;

    // Player homes should not be synced, so that chest contents,
    // which are often used as storage, are never accidentally wiped.
    if (!World::Get().GetServerSettings().SyncPlayerHomes && IsPlayerHome(pCell))
        return;

    GameId cellId{};
    if (!m_world.GetModSystem().GetServerModId(pCell->GetFormIdData(), cellId))
    {
        spdlog::error("Server cell id not found for cell form id {:X}", pCell->GetFormIdData());
        return;
    }

    GameId worldSpaceId{};
    if (TESWorldSpace* pWorldSpace = pPlayer->GetWorldSpace())
    {
        if (!m_world.GetModSystem().GetServerModId(pWorldSpace->GetFormIdData(), worldSpaceId))
        {
            spdlog::error("Server world space id not found for world space form id {:X}", pWorldSpace->GetFormIdData());
            return;
        }
    }

    Vector<FormType> formTypes = {FormType::Container, FormType::Door};
    // Door seemed to be at the wrong form id (29, now 32), verify this.
    Vector<TESObjectREFR*> objects = pCell->GetRefsByFormTypes(formTypes);

    AssignObjectsRequest request{};

    for (TESObjectREFR* pObject : objects)
    {
        if (!ShouldSyncObject(pObject))
        {
            spdlog::warn("Excluding sync for {:X}", pObject->GetFormIdData());
            continue;
        }

        ObjectData objectData{};
        objectData.CellId = cellId;
        objectData.WorldSpaceId = worldSpaceId;
        const auto& position = pObject->GetPositionData();
        objectData.CurrentCoords = GridCellCoords::CalculateGridCellCoords(position.x, position.y);

        if (!m_world.GetModSystem().GetServerModId(pObject->GetFormIdData(), objectData.Id))
        {
            spdlog::error("Server form id not found for object with form id {:X}", pObject->GetFormIdData());
            continue;
        }

        if (Lock* pLock = pObject->GetLock())
        {
            objectData.CurrentLockData.IsLocked = pLock->IsLocked();
            objectData.CurrentLockData.LockLevel = pLock->GetLockLevelData();
        }

        const auto* pBaseForm = pObject->GetBaseFormData();
        if (pBaseForm && pBaseForm->GetFormTypeData() == FormType::Container)
            objectData.CurrentInventory = pObject->GetInventory();

        request.Objects.push_back(objectData);
    }

    m_transport.Send(request);
}
catch (...)
{
    LogObjectServiceFailure("Object cell synchronization failed");
}

void ObjectService::OnAssignObjectsResponse(const AssignObjectsResponse& acMessage) noexcept try
{
    if (!acMessage.IsDecodedValid || acMessage.Objects.size() > AssignObjectsResponse::kMaximumWireObjects)
        return;

    std::array<ObjectResponseIdentity, AssignObjectsResponse::kMaximumWireObjects> identities{};
    std::size_t identityCount{};
    for (const ObjectData& objectData : acMessage.Objects)
    {
        if (!objectData.IsDecodedValid || objectData.ServerId == 0 || !objectData.Id)
        {
            LogObjectServiceFailure("Rejected object assignment response with a zero or invalid object identity");
            return;
        }

        for (std::size_t index = 0; index < identityCount; ++index)
        {
            const auto& identity = identities[index];
            if (identity.ServerId == objectData.ServerId || identity.FormId == objectData.Id)
            {
                LogObjectServiceFailure("Rejected object assignment response with duplicate or contradictory object identities");
                return;
            }
        }

        identities[identityCount++] = ObjectResponseIdentity{objectData.ServerId, objectData.Id};
    }

    for (const ObjectData& objectData : acMessage.Objects)
    {
        const uint32_t cObjectId = World::Get().GetModSystem().GetGameId(objectData.Id);
        if (cObjectId == 0)
        {
            spdlog::error("Object form id could not be resolved: {:X}", objectData.Id.LogFormat());
            continue;
        }

        TESObjectREFR* pObject = Cast<TESObjectREFR>(TESForm::GetById(cObjectId));
        if (!pObject || pObject->GetFormIdData() == 0)
        {
            spdlog::error("Object not found for form id {:X}", objectData.Id.LogFormat());
            continue;
        }

        if (CreateObjectEntity(pObject->GetFormIdData(), objectData.ServerId) == entt::null)
            continue;

        if (objectData.IsSenderFirst)
            continue;

        if (objectData.CurrentLockData != LockData{})
        {
            Lock* pLock = pObject->GetLock();

            if (!pLock)
            {
                pLock = pObject->CreateLock();
                if (!pLock)
                    continue;
            }

            pLock->SetLockLevelData(objectData.CurrentLockData.LockLevel);
            pLock->SetLock(objectData.CurrentLockData.IsLocked);
            pObject->LockChange();
        }

        const auto* pBaseForm = pObject->GetBaseFormData();
        if (pBaseForm && pBaseForm->GetFormTypeData() == FormType::Container)
        {
            Inventory currentInventory = pObject->GetInventory();

            if (currentInventory.ContainsQuestItems())
                pObject->SetInventoryRetainingQuestItems(currentInventory, objectData.CurrentInventory);
            else
                pObject->SetInventory(objectData.CurrentInventory);
        }
    }
}
catch (...)
{
    LogObjectServiceFailure("Object assignment response application failed");
}

entt::entity ObjectService::CreateObjectEntity(const uint32_t acFormId, const uint32_t acServerId) noexcept
{
    if (acFormId == 0 || acServerId == 0)
        return entt::null;

    entt::entity createdEntity{entt::null};
    try
    {
        entt::entity serverEntity{entt::null};
        const auto objectView = m_world.view<ObjectComponent>();
        for (const entt::entity entity : objectView)
        {
            if (objectView.get<ObjectComponent>(entity).Id != acServerId)
                continue;

            if (serverEntity != entt::null)
            {
                LogObjectServiceFailure("Object entity creation rejected duplicate local server identities");
                return entt::null;
            }
            serverEntity = entity;
        }

        entt::entity formEntity{entt::null};
        const auto formView = m_world.view<FormIdComponent>();
        for (const entt::entity entity : formView)
        {
            if (formView.get<FormIdComponent>(entity).Id != acFormId)
                continue;

            if (formEntity != entt::null)
            {
                LogObjectServiceFailure("Object entity creation rejected duplicate local form identities");
                return entt::null;
            }
            formEntity = entity;
        }

        if (serverEntity != entt::null)
        {
            const auto* pFormIdComponent = m_world.try_get<FormIdComponent>(serverEntity);
            if (!pFormIdComponent || pFormIdComponent->Id != acFormId || serverEntity != formEntity)
            {
                LogObjectServiceFailure("Object entity creation rejected a conflicting or partial local mapping");
                return entt::null;
            }
        }

        if (formEntity != entt::null)
        {
            const auto* pObjectComponent = m_world.try_get<ObjectComponent>(formEntity);
            if (!pObjectComponent || pObjectComponent->Id != acServerId || formEntity != serverEntity)
            {
                LogObjectServiceFailure("Object entity creation rejected a conflicting or partial local mapping");
                return entt::null;
            }
        }

        if (serverEntity != entt::null)
            return serverEntity;

        createdEntity = m_world.create();
        m_world.emplace<FormIdComponent>(createdEntity, acFormId);
        m_world.emplace<ObjectComponent>(createdEntity, acServerId);

        try
        {
            spdlog::info("Created object entity, server id: {:X}, form id {:X}", acServerId, acFormId);
        }
        catch (...)
        {
        }

        return createdEntity;
    }
    catch (...)
    {
        if (createdEntity != entt::null)
        {
            try
            {
                m_world.destroy(createdEntity);
            }
            catch (...)
            {
            }
        }

        LogObjectServiceFailure("Object entity creation failed; discarded the partial entity");
        return entt::null;
    }
}

void ObjectService::OnActivate(const ActivateEvent& acEvent) noexcept try
{
    if (!acEvent.pObject)
        return;

    if (acEvent.ActivateFlag)
    {
        acEvent.pObject->Activate(acEvent.pActivator, acEvent.Unk1, acEvent.pObjectToGet, acEvent.Count, acEvent.DefaultProcessing);
    }

    if (!m_transport.IsConnected())
        return;

    if (!acEvent.pActivator)
        return;

    if (Lock* pLock = acEvent.pObject->GetLock())
    {
        if (pLock->GetFlagsData() & 0xFF)
            return;
    }

    ActivateRequest request;

    if (!m_world.GetModSystem().GetServerModId(acEvent.pObject->GetFormIdData(), request.Id))
    {
        spdlog::error("Server form id not found for object form id {:X}", acEvent.pObject->GetFormIdData());
        return;
    }

    TESObjectCELL* pCell = acEvent.pObject->GetParentCellEx();
    if (!pCell)
    {
        spdlog::error("Activated object has no parent cell: {:X}", acEvent.pObject->GetFormIdData());
        return;
    }

    if (!m_world.GetModSystem().GetServerModId(pCell->GetFormIdData(), request.CellId))
    {
        spdlog::error("Server cell id not found for cell form id {:X}", pCell->GetFormIdData());
        return;
    }

    auto view = m_world.view<FormIdComponent>();
    const auto pEntity =
        std::find_if(std::begin(view), std::end(view), [id = acEvent.pActivator->GetFormIdData(), view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == id; });

    if (pEntity == std::end(view))
    {
        // spdlog::error("Activator entity not found for form id {:X}", acEvent.pActivator->GetFormIdData());
        return;
    }

    std::optional<uint32_t> serverIdRes = Utils::GetServerId(*pEntity);
    if (!serverIdRes.has_value())
        return;

    request.ActivatorId = serverIdRes.value();
    request.PreActivationOpenState = acEvent.PreActivationOpenState;

    m_transport.Send(request);
}
catch (...)
{
    LogObjectServiceFailure("Object activation handling failed");
}

void ObjectService::OnActivateNotify(const NotifyActivate& acMessage) noexcept try
{
    Actor* pActor = Utils::GetByServerId<Actor>(acMessage.ActivatorId);
    if (!pActor)
    {
        spdlog::error("{}: could not find actor server id {:X}", __FUNCTION__, acMessage.ActivatorId);
        return;
    }

    const uint32_t cObjectId = World::Get().GetModSystem().GetGameId(acMessage.Id);
    TESObjectREFR* pObject = Cast<TESObjectREFR>(TESForm::GetById(cObjectId));
    if (!pObject)
    {
        spdlog::error("Failed to retrieve object to activate.");
        return;
    }

    const auto* pBaseForm = pObject->GetBaseFormData();
    if (pBaseForm && pBaseForm->GetFormTypeData() == FormType::Door)
    {
        auto remotePreActivationState = static_cast<TESObjectREFR::OpenState>(acMessage.PreActivationOpenState);
        TESObjectREFR::OpenState localState = pObject->GetOpenState();

        if (remotePreActivationState != localState)
        {
            // The doors are unsynced at this point. If we'll Activate the one on our side
            // it'll just continue to be unsynced (open remotely, closed locally and vice versa)
            return;
        }
    }

    // unsure if these flags are the best, but these are passed with the papyrus Activate fn
    // might be an idea to have the client send the flags through NotifyActivate
    pObject->Activate(pActor, 0, nullptr, 1, 0);
}
catch (...)
{
    LogObjectServiceFailure("Object activation notification handling failed");
}

void ObjectService::OnLockChange(const LockChangeEvent& acEvent) noexcept try
{
    if (!m_transport.IsConnected())
        return;

    LockChangeRequest request;

    if (!m_world.GetModSystem().GetServerModId(acEvent.FormId, request.Id))
    {
        spdlog::error("Server form id for lock object not found, form id: {:X}", acEvent.FormId);
        return;
    }

    const auto* const pObject = Cast<TESObjectREFR>(TESForm::GetById(acEvent.FormId));
    if (!pObject)
    {
        spdlog::error("Lock-change object not found for form id {:X}", acEvent.FormId);
        return;
    }

    TESObjectCELL* pCell = pObject->GetParentCellEx();
    if (!pCell)
    {
        spdlog::error("Activated object has no parent cell: {:X}", pObject->GetFormIdData());
        return;
    }

    if (!m_world.GetModSystem().GetServerModId(pCell->GetFormIdData(), request.CellId))
    {
        spdlog::error("Server cell id for cell not found, cell form id: {:X}", pCell->GetFormIdData());
        return;
    }

    request.IsLocked = acEvent.IsLocked;
    request.LockLevel = acEvent.LockLevel;

    m_transport.Send(request);
}
catch (...)
{
    LogObjectServiceFailure("Object lock-change handling failed");
}

void ObjectService::OnLockChangeNotify(const NotifyLockChange& acMessage) noexcept try
{
    const auto cObjectId = World::Get().GetModSystem().GetGameId(acMessage.Id);
    if (cObjectId == 0)
    {
        spdlog::error("Failed to retrieve object id to (un)lock.");
        return;
    }

    auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(cObjectId));
    if (!pObject)
    {
        spdlog::error("Failed to retrieve object to (un)lock.");
        return;
    }

    auto* pLock = pObject->GetLock();

    if (!acMessage.IsLocked)
    {
        if (!pLock || !pLock->IsLocked())
            return;
    }

    if (!pLock && acMessage.IsLocked)
    {
        pLock = pObject->CreateLock();
        if (!pLock)
        {
            spdlog::error("Failed to create lock for object form id {:X}", pObject->GetFormIdData());
            return;
        }
    }

    pLock->SetLockLevelData(acMessage.LockLevel);
    pLock->SetLock(acMessage.IsLocked);
    pObject->LockChange();
}
catch (...)
{
    LogObjectServiceFailure("Object lock-change notification handling failed");
}

void ObjectService::OnScriptAnimationEvent(const ScriptAnimationEvent& acEvent) noexcept try
{
    ScriptAnimationRequest request{};
    request.FormID = acEvent.FormID;
    request.Animation = acEvent.Animation;
    request.EventName = acEvent.EventName;

    m_transport.Send(request);
}
catch (...)
{
    LogObjectServiceFailure("Object script-animation relay failed");
}

void ObjectService::OnNotifyScriptAnimation(const NotifyScriptAnimation& acMessage) noexcept try
{
    if (acMessage.FormID == 0)
        return;

    auto* pForm = TESForm::GetById(acMessage.FormID);
    auto* pObject = Cast<TESObjectREFR>(pForm);

    if (!pObject)
    {
        spdlog::error("Failed to fetch notify script animation object, form id: {:X}", acMessage.FormID);
        return;
    }

    BSFixedString eventName(acMessage.EventName.c_str());
    if (acMessage.Animation == String{})
    {
        pObject->PlayAnimation(&eventName);
    }
    else
    {
        BSFixedString animation(acMessage.Animation.c_str());
        pObject->PlayAnimationAndWait(&animation, &eventName);
    }
}
catch (...)
{
    LogObjectServiceFailure("Object script-animation notification handling failed");
}

BSTEventResult ObjectService::OnEvent(const TESActivateEvent* acEvent, const EventDispatcher<TESActivateEvent>* aDispatcher)
{
#if ENVIRONMENT_DEBUG
    auto view = m_world.view<ObjectComponent>();
    auto* pActivatedObject = acEvent ? acEvent->GetObjectActivatedData() : nullptr;
    if (!pActivatedObject)
        return BSTEventResult::kOk;

    const auto itor =
        std::find_if(std::begin(view), std::end(view), [id = pActivatedObject->GetFormIdData(), view](entt::entity entity) { return view.get<ObjectComponent>(entity).Id == id; });

    if (itor == std::end(view))
    {
        AddObjectComponent(pActivatedObject);
    }
#endif

    return BSTEventResult::kOk;
}
