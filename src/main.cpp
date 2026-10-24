#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <iostream>
#include <string>
#include <cstdio>
#include <thread>
#include <chrono>

#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#endif

using namespace geode::prelude;

namespace BufferVortex {
    // Stage selector matching the video's F6 / F7 modes
    int stageGround = 2; // Default: 8 Ticks
    int stageWave   = 2; // Default: 8 Ticks
    int stageShip   = 0; // Default: OFF
    bool hazardSaver = true;
    bool threadRunning = false;

    int pendingPressP1 = 0;
    int pendingPressP2 = 0;
    bool isHoldingP1 = false;
    bool isHoldingP2 = false;

    bool internalLock = false;

    int stageToTicks(int stage) {
        switch (stage) {
            case 1: return 4;
            case 2: return 8;
            case 3: return 16; // VORTEX
            case 4: return 32;
            default: return 0;
        }
    }

    void writeCMD(const std::string& msg) {
#ifdef GEODE_IS_WINDOWS
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut && hOut != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteConsoleA(hOut, msg.c_str(), static_cast<DWORD>(msg.length()), &written, NULL);
        }
#endif
        std::cout << msg << std::flush;
    }

    void printBanner() {
        char buf[1024];
        std::snprintf(buf, sizeof(buf),
            "\n================================================================================\n"
            "   [BUFFER] Geode Predictive Input Buffer - Live Developer Console\n"
            "================================================================================\n"
            "   [F1] Ground / Orb / Pad Ticks : Stage %d (%d Ticks)\n"
            "   [F2] Wave Corridor Ticks      : Stage %d (%d Ticks)\n"
            "   [F3] Ship / UFO / Swing Ticks : Stage %d (%d Ticks)\n"
            "   [F4] Hazard Spike Interceptor : %s\n"
            "   [F6] Cycle Modes | [F7] VORTEX MODE (Instant 16 Ticks)\n"
            "================================================================================\n",
            stageGround, stageToTicks(stageGround),
            stageWave, stageToTicks(stageWave),
            stageShip, stageToTicks(stageShip),
            hazardSaver ? "ENABLED (ACTIVE)" : "DISABLED"
        );
        writeCMD(buf);
    }

    void logAction(const char* type, const char* mode, int ticks, const char* playerStr) {
        char buf[256];
        if (std::string(type) == "SAVE") {
            std::snprintf(buf, sizeof(buf), "[BUFFER] *** SAVED AT %d TICKS HAZARD (%s) [%s] (Deflected) ***\n", ticks, playerStr, mode);
        } else {
            std::snprintf(buf, sizeof(buf), "[BUFFER] [INPUT] (%s) registered -> %d Ticks Window [%s]\n", mode, ticks, playerStr);
        }
        writeCMD(buf);
    }

    bool isWave(PlayerObject* p) {
        return p && p->m_isDart;
    }

    bool isContinuous(PlayerObject* p) {
        return p && (p->m_isShip || p->m_isDart || p->m_isSwing);
    }

    int getTicksForPlayer(PlayerObject* p) {
        if (!p) return 0;
        if (p->m_isDart) return stageToTicks(stageWave);
        if (p->m_isShip || p->m_isBird || p->m_isSwing) return stageToTicks(stageShip);
        return stageToTicks(stageGround);
    }

    void handleHotkey(int key) {
        if (key == 1) { // F1
            stageGround = (stageGround + 1) % 5;
        } else if (key == 2) { // F2
            stageWave = (stageWave + 1) % 5;
        } else if (key == 3) { // F3
            stageShip = (stageShip + 1) % 5;
        } else if (key == 4) { // F4
            hazardSaver = !hazardSaver;
        } else if (key == 6) { // F6 (Cycle all)
            stageGround = (stageGround + 1) % 5;
            stageWave = (stageWave + 1) % 5;
        } else if (key == 7) { // F7 (VORTEX INSTANT 16t)
            stageGround = 3;
            stageWave = 3;
            stageShip = 3;
            hazardSaver = true;
            writeCMD("[BUFFER] *** VORTEX MODE ENGAGED (16 Ticks All Modes) ***\n");
        }
        printBanner();
    }

    void startListener() {
        if (threadRunning) return;
        threadRunning = true;
#ifdef GEODE_IS_WINDOWS
        std::thread([]() {
            bool f1 = false, f2 = false, f3 = false, f4 = false, f6 = false, f7 = false;
            while (true) {
                auto poll = [](int vk, bool& st, int id) {
                    if (GetAsyncKeyState(vk) & 0x8000) {
                        if (!st) { st = true; handleHotkey(id); }
                    } else { st = false; }
                };
                poll(VK_F1, f1, 1);
                poll(VK_F2, f2, 2);
                poll(VK_F3, f3, 3);
                poll(VK_F4, f4, 4);
                poll(VK_F6, f6, 6);
                poll(VK_F7, f7, 7);
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
        }).detach();
#endif
    }
}

$on_mod(Loaded) {
#ifdef GEODE_IS_WINDOWS
    if (AllocConsole()) {
        FILE* fDummy = nullptr;
        freopen_s(&fDummy, "CONOUT$", "w", stdout);
        freopen_s(&fDummy, "CONOUT$", "w", stderr);
    }
    SetConsoleTitleA("Geode - Predictive Input Buffer - Live Console");
#endif
    BufferVortex::printBanner();
    BufferVortex::startListener();
}

// 1. Raw Input Hook (Catches human clicks and seeds the buffer)
class $modify(BufferPlayerHook, PlayerObject) {
    bool pushButton(PlayerButton btn) {
        if (BufferVortex::internalLock || btn != PlayerButton::Jump) {
            return PlayerObject::pushButton(btn);
        }

        auto pl = PlayLayer::get();
        if (pl) {
            int ticks = BufferVortex::getTicksForPlayer(this);
            bool isP1 = (this == pl->m_player1);
            if (isP1) {
                BufferVortex::isHoldingP1 = true;
                if (ticks > 0) BufferVortex::pendingPressP1 = ticks;
            } else {
                BufferVortex::isHoldingP2 = true;
                if (ticks > 0) BufferVortex::pendingPressP2 = ticks;
            }
            BufferVortex::logAction("INPUT", isWave(this) ? "WAVE" : "GROUND/CUBE", ticks, isP1 ? "P1" : "P2");
        }

        return PlayerObject::pushButton(btn);
    }

    bool releaseButton(PlayerButton btn) {
        if (BufferVortex::internalLock || btn != PlayerButton::Jump) {
            return PlayerObject::releaseButton(btn);
        }

        auto pl = PlayLayer::get();
        if (pl) {
            if (this == pl->m_player1) BufferVortex::isHoldingP1 = false;
            else if (this == pl->m_player2) BufferVortex::isHoldingP2 = false;
        }

        return PlayerObject::releaseButton(btn);
    }
};

// 2. Sub-Tick Execution Engine (Resolves orb spam and landing pre-emption)
class $modify(BufferGameHook, GJBaseGameLayer) {
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);

        auto pl = PlayLayer::get();
        if (!pl || this != static_cast<GJBaseGameLayer*>(pl)) return;

        auto stepBuffer = [&](PlayerObject* player, int& pendingPress, bool isHolding) {
            if (!player || player->m_isDead) return;
            if (pendingPress > 0) {
                pendingPress--;
                BufferVortex::internalLock = true;

                if (player->m_isOnGround || player->m_isBird || player->m_isSwing) {
                    player->pushButton(PlayerButton::Jump);
                    if (player->m_isOnGround && !BufferVortex::isContinuous(player)) {
                        pendingPress = 0;
                    }
                } else {
                    // Pre-emptively trigger jump rings / pads during active buffer window (Orb Spam fix)
                    player->ringJump(nullptr, false);
                }

                BufferVortex::internalLock = false;
            }
        };

        stepBuffer(pl->m_player1, BufferVortex::pendingPressP1, BufferVortex::isHoldingP1);
        if (pl->m_gameState.m_isDualMode) {
            stepBuffer(pl->m_player2, BufferVortex::pendingPressP2, BufferVortex::isHoldingP2);
        }
    }
};

// 3. Hazard Interceptor & Safety Net (Prints attempts, catches deaths, deflects hitboxes)
class $modify(BufferPlayLayerHook, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        BufferVortex::pendingPressP1 = 0;
        BufferVortex::pendingPressP2 = 0;
        BufferVortex::isHoldingP1 = false;
        BufferVortex::isHoldingP2 = false;
        return true;
    }

    void resetLevel() {
        BufferVortex::pendingPressP1 = 0;
        BufferVortex::pendingPressP2 = 0;
        BufferVortex::isHoldingP1 = false;
        BufferVortex::isHoldingP2 = false;
        PlayLayer::resetLevel();
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        // Allow anticheat test spike on level start
        if (!player || object == m_anticheatSpike) {
            PlayLayer::destroyPlayer(player, object);
            return;
        }

        if (!BufferVortex::hazardSaver) {
            PlayLayer::destroyPlayer(player, object);
            return;
        }

        int activeTicks = BufferVortex::getTicksForPlayer(player);
        if (activeTicks <= 0) {
            PlayLayer::destroyPlayer(player, object);
            return;
        }

        bool isP1 = (player == m_player1);
        bool isHolding = isP1 ? BufferVortex::isHoldingP1 : BufferVortex::isHoldingP2;

        BufferVortex::internalLock = true;
        player->m_isDead = false;
        double gravDir = player->m_isUpsideDown ? -1.0 : 1.0;

        if (BufferVortex::isWave(player)) {
            // Wave Corridor Deflection
            if (isHolding) {
                player->releaseButton(PlayerButton::Jump);
                player->setPositionY(player->getPositionY() - 3.5f * static_cast<float>(gravDir));
            } else {
                player->pushButton(PlayerButton::Jump);
                player->setPositionY(player->getPositionY() + 3.5f * static_cast<float>(gravDir));
            }
            BufferVortex::logAction("SAVE", "WAVE CORRIDOR", activeTicks, isP1 ? "P1" : "P2");
            BufferVortex::internalLock = false;
            return;
        }

        if (player->m_isShip || player->m_isSwing) {
            // Ship / Swing Flight Deflection
            if (isHolding) {
                player->releaseButton(PlayerButton::Jump);
                player->setPositionY(player->getPositionY() - 4.0f * static_cast<float>(gravDir));
            } else {
                player->pushButton(PlayerButton::Jump);
                player->setPositionY(player->getPositionY() + 4.0f * static_cast<float>(gravDir));
            }
            BufferVortex::logAction("SAVE", "SHIP/SWING", activeTicks, isP1 ? "P1" : "P2");
            BufferVortex::internalLock = false;
            return;
        }

        // Cube, Robot, Spider, Ball, UFO Frame-Perfect Spike Intercept
        player->m_isOnGround = true;
        player->pushButton(PlayerButton::Jump);
        player->ringJump(nullptr, false);

        if (player->m_yVelocity * gravDir < 10.0) {
            player->m_yVelocity = 11.5 * gravDir;
        }
        player->setPositionY(player->getPositionY() + 4.0f * static_cast<float>(gravDir));

        BufferVortex::logAction("SAVE", "GROUND / FRAME PERFECT", activeTicks, isP1 ? "P1" : "P2");
        BufferVortex::internalLock = false;
    }
};
