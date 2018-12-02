#include "Client.hpp"

#include <cstdint>
#include <cstring>

#include "Features/Hud/InputHud.hpp"
#include "Features/Imitator.hpp"
#include "Features/OffsetFinder.hpp"
#include "Features/ReplaySystem/ReplayPlayer.hpp"
#include "Features/ReplaySystem/ReplayProvider.hpp"
#include "Features/ReplaySystem/ReplayRecorder.hpp"
#include "Features/Session.hpp"
#include "Features/Tas/CommandQueuer.hpp"

#include "Console.hpp"
#include "Engine.hpp"
#include "Server.hpp"

#include "Game.hpp"
#include "Interface.hpp"
#include "Offsets.hpp"
#include "Utils.hpp"

Variable cl_showpos;
Variable ui_loadingscreen_transition_time;
Variable hide_gun_when_holding;

REDECL(Client::HudUpdate);
REDECL(Client::CreateMove);
REDECL(Client::CreateMove2);
REDECL(Client::GetName);

void* Client::GetPlayer()
{
    return this->GetClientEntity(this->s_EntityList->ThisPtr(), engine->GetLocalPlayerIndex());
}
Vector Client::GetAbsOrigin()
{
    auto player = this->GetPlayer();
    return (player) ? *(Vector*)((uintptr_t)player + Offsets::C_m_vecAbsOrigin) : Vector();
}
QAngle Client::GetAbsAngles()
{
    auto player = this->GetPlayer();
    return (player) ? *(QAngle*)((uintptr_t)player + Offsets::C_m_angAbsRotation) : QAngle();
}
Vector Client::GetLocalVelocity()
{
    auto player = this->GetPlayer();
    return (player) ? *(Vector*)((uintptr_t)player + Offsets::C_m_vecVelocity) : Vector();
}
Vector Client::GetViewOffset()
{
    auto player = this->GetPlayer();
    return (player) ? *(Vector*)((uintptr_t)player + Offsets::C_m_vecViewOffset) : Vector();
}

// CHLClient::HudUpdate
DETOUR(Client::HudUpdate, unsigned int a2)
{
    if (cmdQueuer->isRunning) {
        for (auto&& tas = cmdQueuer->frames.begin(); tas != cmdQueuer->frames.end();) {
            --tas->framesLeft;

            if (tas->framesLeft <= 0) {
                console->DevMsg("[%i] %s\n", session->currentFrame, tas->command.c_str());

                if (sar.game->version & SourceGame_Portal2Engine) {
                    if (engine->GetMaxClients() <= 1) {
                        engine->Cbuf_AddText(tas->splitScreen, tas->command.c_str(), 0);
                    } else {
                        auto entity = engine->PEntityOfEntIndex(tas->splitScreen + 1);
                        if (entity && !entity->IsFree() && server->IsPlayer(entity->m_pUnk)) {
                            engine->ClientCommand(nullptr, entity, tas->command.c_str());
                        }
                    }
                } else if (sar.game->version & SourceGame_HalfLife2Engine) {
                    engine->AddText(engine->s_CommandBuffer, tas->command.c_str(), 0);
                }

                tas = cmdQueuer->frames.erase(tas);
            } else {
                ++tas;
            }
        }
    }

    ++session->currentFrame;
    return Client::HudUpdate(thisptr, a2);
}

// ClientModeShared::CreateMove
DETOUR(Client::CreateMove, float flInputSampleTime, CUserCmd* cmd)
{
    if (cmd->command_number) {
        if (replayPlayer1->IsPlaying()) {
            replayPlayer1->Play(replayProvider->GetCurrentReplay(), cmd);
        } else if (replayRecorder1->IsRecording()) {
            replayRecorder1->Record(replayProvider->GetCurrentReplay(), cmd);
        }
    }

    if (sar_mimic.isRegistered && sar_mimic.GetBool()) {
        imitator->Save(cmd);
    }

    inputHud->SetButtonBits(cmd->buttons);

    return Client::CreateMove(thisptr, flInputSampleTime, cmd);
}
DETOUR(Client::CreateMove2, float flInputSampleTime, CUserCmd* cmd)
{
    if (cmd->command_number) {
        if (replayPlayer2->IsPlaying()) {
            replayPlayer2->Play(replayProvider->GetCurrentReplay(), cmd);
        } else if (replayRecorder2->IsRecording()) {
            replayRecorder2->Record(replayProvider->GetCurrentReplay(), cmd);
        }
    }

    if (sar_mimic.isRegistered && sar_mimic.GetBool()) {
        imitator->Modify(cmd);
    }

    return Client::CreateMove2(thisptr, flInputSampleTime, cmd);
}

// CHud::GetName
DETOUR_T(const char*, Client::GetName)
{
    // Never allow CHud::FindElement to find this HUD
    if (sar_disable_challenge_stats_hud.GetBool())
        return "";

    return Client::GetName(thisptr);
}

bool Client::Init()
{
    bool readJmp = false;
#ifdef _WIN32
    readJmp = sar.game->version & (SourceGame_TheStanleyParable | SourceGame_TheBeginnersGuide);
#endif

    this->g_ClientDLL = Interface::Create(this->Name(), "VClient0");
    this->s_EntityList = Interface::Create(this->Name(), "VClientEntityList0", false);

    if (this->g_ClientDLL) {
        this->GetAllClasses = this->g_ClientDLL->Original<_GetAllClasses>(Offsets::GetAllClasses, readJmp);

        this->g_ClientDLL->Hook(Client::HudUpdate_Hook, Client::HudUpdate, Offsets::HudUpdate);

        if (sar.game->version == SourceGame_Portal2) {
            auto leaderboard = Command("+leaderboard");
            if (!!leaderboard) {
                using _GetHud = void*(__cdecl*)(int unk);
                using _FindElement = void*(__func*)(void* thisptr, const char* pName);

                auto cc_leaderboard_enable = (uintptr_t)leaderboard.ThisPtr()->m_pCommandCallback;
                auto GetHud = Memory::Read<_GetHud>(cc_leaderboard_enable + Offsets::GetHud);
                auto FindElement = Memory::Read<_FindElement>(cc_leaderboard_enable + Offsets::FindElement);
                auto CHUDChallengeStats = FindElement(GetHud(-1), "CHUDChallengeStats");

                if (this->g_HUDChallengeStats = Interface::Create(CHUDChallengeStats)) {
                    this->g_HUDChallengeStats->Hook(Client::GetName_Hook, Client::GetName, Offsets::GetName);
                }
            }
        } else if (sar.game->version == SourceGame_TheStanleyParable) {
            auto IN_ActivateMouse = this->g_ClientDLL->Original(Offsets::IN_ActivateMouse, readJmp);
            auto g_InputAddr = Memory::DerefDeref<void*>(IN_ActivateMouse + Offsets::g_Input);

            if (auto g_Input = Interface::Create(g_InputAddr, false)) {
                auto GetButtonBits = g_Input->Original(Offsets::GetButtonBits, readJmp);
                Memory::Deref(GetButtonBits + Offsets::in_jump, &this->in_jump);

                auto JoyStickApplyMovement = g_Input->Original(Offsets::JoyStickApplyMovement, readJmp);
                Memory::Read(JoyStickApplyMovement + Offsets::KeyDown, &this->KeyDown);
                Memory::Read(JoyStickApplyMovement + Offsets::KeyUp, &this->KeyUp);
            }
        }

        auto HudProcessInput = this->g_ClientDLL->Original(Offsets::HudProcessInput, readJmp);
        void* clientMode = nullptr;
        void* clientMode2 = nullptr;
        if (sar.game->version & SourceGame_Portal2Engine) {
            if (sar.game->version & (SourceGame_Portal2 | SourceGame_ApertureTag)) {
                auto GetClientMode = Memory::Read<uintptr_t>(HudProcessInput + Offsets::GetClientMode);
                auto g_pClientMode = Memory::Deref<uintptr_t>(GetClientMode + Offsets::g_pClientMode);
                clientMode = Memory::Deref<void*>(g_pClientMode);
                clientMode2 = Memory::Deref<void*>(g_pClientMode + sizeof(void*));
            } else {
                typedef void* (*_GetClientMode)();
                auto GetClientMode = Memory::Read<_GetClientMode>(HudProcessInput + Offsets::GetClientMode);
                clientMode = GetClientMode();
            }
        } else if (sar.game->version & SourceGame_HalfLife2Engine) {
            clientMode = Memory::DerefDeref<void*>(HudProcessInput + Offsets::GetClientMode);
        }

        if (this->g_pClientMode = Interface::Create(clientMode)) {
            this->g_pClientMode->Hook(Client::CreateMove_Hook, Client::CreateMove, Offsets::CreateMove);
        }

        if (this->g_pClientMode2 = Interface::Create(clientMode2)) {
            this->g_pClientMode2->Hook(Client::CreateMove2_Hook, Client::CreateMove2, Offsets::CreateMove);
        }
    }

    if (this->s_EntityList) {
        this->GetClientEntity = this->s_EntityList->Original<_GetClientEntity>(Offsets::GetClientEntity, readJmp);
    }

    offsetFinder->ClientSide("CBasePlayer", "m_vecVelocity[0]", &Offsets::C_m_vecVelocity);
    offsetFinder->ClientSide("CBasePlayer", "m_vecViewOffset[0]", &Offsets::C_m_vecViewOffset);

    return this->hasLoaded = this->g_ClientDLL && this->s_EntityList;
}
void Client::Shutdown()
{
    Interface::Delete(this->g_ClientDLL);
    Interface::Delete(this->g_pClientMode);
    Interface::Delete(this->g_pClientMode2);
    Interface::Delete(this->g_HUDChallengeStats);
    Interface::Delete(this->s_EntityList);
}

Client* client;
