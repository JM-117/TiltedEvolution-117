#include <TiltedOnlinePCH.h>

#include <Games/References.h>

#include <Forms/BGSAction.h>
#include <Forms/TESIdleForm.h>

#include <Structs/ActionEvent.h>

#include <Games/Animation/ActorMediator.h>
#include <Games/Animation/TESActionData.h>

#include <Misc/BSFixedString.h>

#include <World.h>
#include <atomic>

TP_THIS_FUNCTION(TPerformAction, uint8_t, ActorMediator, TESActionData* apAction);
static TPerformAction* RealPerformAction;

// TODO: make scoped override
thread_local bool g_forceAnimation = false;

// DIAGNOSTIC: set by CharacterService around a mount; while open, every action is logged
std::atomic<int64_t> g_mountDiagUntilMs{0};
bool MountDiagActive() noexcept
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() < g_mountDiagUntilMs.load();
}

uint8_t TP_MAKE_THISCALL(HookPerformAction, ActorMediator, TESActionData* apAction)
{
    auto pActor = apAction->actor;
    const auto pExtension = pActor->GetExtension();

    if (MountDiagActive() && apAction->action && apAction->action->formID != 0x46BAF && apAction->action->formID != 0x13002)
        spdlog::info("MountAction actor {:X} local={} forced={} action {:X} event '{}' target '{}' idle {:X} type {} flag {} state {:X}/{:X}", pActor->formID, pExtension->IsLocal(), g_forceAnimation,
                     apAction->action ? apAction->action->formID : 0, apAction->eventName.AsAscii(), apAction->targetEventName.AsAscii(), apAction->idleForm ? apAction->idleForm->formID : 0,
                     apAction->unkInput, apAction->someFlag, pActor->actorState.flags1, pActor->actorState.flags2);

    if (!pExtension->IsRemote() || g_forceAnimation)
    {
        ActionEvent action;
        action.State1 = pActor->actorState.flags1;
        action.State2 = pActor->actorState.flags2;
        action.Type = apAction->unkInput | (apAction->someFlag ? 0x4 : 0);
        action.Tick = World::Get().GetTick();
        action.ActorId = pActor->formID;
        action.ActionId = apAction->action->formID;
        action.TargetId = apAction->target ? apAction->target->formID : 0;

        pActor->SaveAnimationVariables(action.Variables);

        const auto res = TiltedPhoques::ThisCall(RealPerformAction, apThis, apAction);

        // spdlog::debug("Action event name: {}, target name: {}", apAction->eventName.AsAscii(), apAction->targetEventName.AsAscii());

        // This is a weird case where it gets spammed and doesn't do much, not sure if it still needs to be sent over the network
        if (apAction->someFlag == 1 || g_forceAnimation)
            return res;

        action.EventName = apAction->eventName.AsAscii();
        action.TargetEventName = apAction->targetEventName.AsAscii();
        action.IdleId = apAction->idleForm ? apAction->idleForm->formID : 0;

        // Save for later
        if (res)
        {
            pExtension->LatestAnimation = action;
        }

        World::Get().GetRunner().Trigger(action);

        return res;
    }

    return 0;
}

ActorMediator* ActorMediator::Get() noexcept
{
    POINTER_SKYRIMSE(ActorMediator*, s_actorMediator, 403567);

    return *(s_actorMediator.Get());
}

bool ActorMediator::PerformAction(TESActionData* apAction) noexcept
{
    const auto res = TiltedPhoques::ThisCall(RealPerformAction, this, apAction);

    return res != 0;
}

bool ActorMediator::ForceAction(TESActionData* apAction) noexcept
{
    TP_THIS_FUNCTION(TAnimationStep, uint8_t, ActorMediator, TESActionData*);
    using TApplyAnimationVariables = void*(void*, TESActionData*);

    POINTER_SKYRIMSE(TApplyAnimationVariables, ApplyAnimationVariables, 39004);
    POINTER_SKYRIMSE(TAnimationStep, PerformComplexAction, 38953);
    POINTER_SKYRIMSE(void*, qword_142F271B8, 403566);

    uint8_t result = 0;

    auto pActor = static_cast<Actor*>(apAction->actor);
    if (pActor)
    {
        result = TiltedPhoques::ThisCall(PerformComplexAction, this, apAction);

        ApplyAnimationVariables(*qword_142F271B8.Get(), apAction);
    }

    return result;
}

ActionInput::ActionInput(uint32_t aParam1, Actor* apActor, BGSAction* apAction, TESObjectREFR* apTarget)
{
    // skip vtable as we never use this directly
    actor = apActor;
    target = apTarget;
    action = apAction;
    unkInput = aParam1;
}

void ActionInput::Release()
{
    actor.Release();
    target.Release();
}

ActionOutput::ActionOutput()
    : eventName("")
    , targetEventName("")
{
    // skip vtable as we never use this directly

    result = 0;
    targetIdleForm = nullptr;
    idleForm = nullptr;
    unk1 = 0;
}

void ActionOutput::Release()
{
    eventName.Release();
    targetEventName.Release();
}

BGSActionData::BGSActionData(uint32_t aParam1, Actor* apActor, BGSAction* apAction, TESObjectREFR* apTarget)
    : ActionInput(aParam1, apActor, apAction, apTarget)
{
    // skip vtable as we never use this directly
    someFlag = 0;
}

TESActionData::TESActionData(uint32_t aParam1, Actor* apActor, BGSAction* apAction, TESObjectREFR* apTarget)
    : BGSActionData(aParam1, apActor, apAction, apTarget)
{
    POINTER_SKYRIMSE(void*, s_vtbl, 188603);

    someFlag = false;

    *reinterpret_cast<void**>(this) = s_vtbl.Get();
}

TESActionData::~TESActionData()
{
    ActionOutput::Release();
    ActionInput::Release();
}

static TiltedPhoques::Initializer s_animationHook(
    []()
    {
        POINTER_SKYRIMSE(TPerformAction, performAction, 38949);

        RealPerformAction = performAction.Get();

        TP_HOOK(&RealPerformAction, HookPerformAction);
    });
