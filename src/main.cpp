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

namespace BufferEngine {
    int masterStage = 3; // 3 = VORTEX (16 Ticks lookahead)
    bool threadRunning = false;

    int pressBufferP1 = 0;
    int pressBufferP2 = 0;
    bool internalBypass = false;

    void sendToCMD(const std::string& text) {
#ifdef GEODE_IS_WINDOWS
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut && hOut != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteConsoleA(hOut, text.c_str(), static_cast<DWORD>(text.size()), &written, nullptr);
            return;
        }
#endif
        std::cout << text << std::flush;
    }

    int getTicks(bool isContinuous) {
        if (masterStage == 0) return 0;
        switch (masterStage) {
            case 1: return 6;   // Light
            case 2: return 12;  // Medium
            case 3: return 20;  // Vortex (Strong lookahead)
            case 4: return 35;  // Extreme
            case 5:
            default: {
                int val = isContinuous
                    ? static_cast<int>(Mod::get()->getSettingValue<int64_t>("custom-wave-ticks"))
                    : static_cast<int>(Mod::get()->getSettingValue<int64_t>("custom-ticks"));
                return (val > 0) ? val : 20;
            }
        }
    }

    void printHUD() {
        char buf[512];
        std::snprintf(buf, sizeof(buf),
            "\n[BUFFER VORTEX] Active Stage: %d | Ground Ticks: %d | Wave Ticks: %d\n"
            "Controls: [F6] Cycle Stage | [F7] Toggle Vortex/Custom\n",
            masterStage, getTicks(false), getTicks(true));
        sendToCMD(buf);
    }

    void handleHotkey(int key) {
        if (key == 6) { // F6
            masterStage = (masterStage + 1) % 6;
            printHUD();
        } else if (key == 7) { // F7
            masterStage = (masterStage == 3) ? 5 : 3;
            printHUD();
        }
    }

    void initConsole() {
#ifdef GEODE_IS_WINDOWS
        if (AllocConsole()) {
            FILE* fDummy = nullptr;
            freopen_s(&fDummy, "CONOUT$", "w", stdout);
            freopen_s(&fDummy, "CONOUT$", "w", stderr);
        }
        SetConsoleTitleA("Geode - Predictive Buffer Vortex");
#endif
        printHUD();

        if (!threadRunning) {
            threadRunning = true;
#ifdef GEODE_IS_WINDOWS
            std::thread([]() {
                bool f6State = false, f7State = false;
                while (true) {
                    if (GetAsyncKeyState(VK_F6) & 0x8000) {
                        if (!f6State) { f6State = true; handleHotkey(6); }
                    } else { f6State = false; }

                    if (GetAsyncKeyState(VK_F7) & 0x8000) {
                        if (!f7State) { f7State = true; handleHotkey(7); }
                    } else { f7State = false; }

                    std::this_thread::sleep_for(std::chrono::milliseconds(30));
                }
            }).detach();
#endif
        }
    }
}

$on_mod(Loaded) {
    BufferEngine::initConsole();
}

// 1. Capture raw player input and seed the predictive buffer countdown
class $modify(BufferPlayerHook, PlayerObject) {
    bool pushButton(PlayerButton btn) {
        if (BufferEngine::internalBypass || btn != PlayerButton::Jump) {
            return PlayerObject::pushButton(btn);
        }

        auto pl = PlayLayer::get();
        if (pl && BufferEngine::masterStage != 0) {
            bool isCont = (m_isDart || m_isShip || m_isSwing);
            int ticks = BufferEngine::getTicks(isCont);
            if (this == pl->m_player1) BufferEngine::pressBufferP1 = ticks;
            else if (this == pl->m_player2) BufferEngine::pressBufferP2 = ticks;
        }

        return PlayerObject::pushButton(btn);
    }
};

// 2. Sub-tick execution engine (Processes orb spam, pad hits, and pre-emptive jumps)
class $modify(BufferCommandHook, GJBaseGameLayer) {
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);

        auto pl = PlayLayer::get();
        if (!pl || this != static_cast<GJBaseGameLayer*>(pl)) return;
        if (BufferEngine::masterStage == 0) return;

        auto tickPlayer = [&](PlayerObject* p, int& pressBuf) {
            if (!p || p->m_isDead) return;

            if (pressBuf > 0) {
                pressBuf--;
                BufferEngine::internalBypass = true;

                if (p->m_isDart || p->m_isShip || p->m_isSwing) {
                    p->pushButton(PlayerButton::Jump);
                } else {
                    if (p->m_isOnGround) {
                        p->pushButton(PlayerButton::Jump);
                        pressBuf = 0;
                    } else {
                        p->ringJump(nullptr, false);
                    }
                }

                BufferEngine::internalBypass = false;
            }
        };

        tickPlayer(pl->m_player1, BufferEngine::pressBufferP1);
        if (pl->m_gameState.m_isDualMode) {
            tickPlayer(pl->m_player2, BufferEngine::pressBufferP2);
        }
    }
};

// 3. The Real Hazard Interceptor: Catches lethal hits, alters physics, and logs every save!
class $modify(BufferPlayLayerHook, PlayLayer) {
    void resetLevel() {
        BufferEngine::pressBufferP1 = 0;
        BufferEngine::pressBufferP2 = 0;
        PlayLayer::resetLevel();
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        // Let RobTop's initial check spike pass safely
        if (!player || object == m_anticheatSpike) {
            PlayLayer::destroyPlayer(player, object);
            return;
        }

        if (BufferEngine::masterStage == 0) {
            PlayLayer::destroyPlayer(player, object);
            return;
        }

        bool isCont = (player->m_isDart || player->m_isShip || player->m_isSwing);
        int maxTicks = BufferEngine::getTicks(isCont);

        if (maxTicks <= 0) {
            PlayLayer::destroyPlayer(player, object);
            return;
        }

        // Intercept death, cancel destruction, alter state, and save the player!
        BufferEngine::internalBypass = true;
        player->m_isDead = false;
        double grav = player->m_isUpsideDown ? -1.0 : 1.0;

        char logMsg[256];

        if (player->m_isDart) {
            // Wave corridor deflection
            player->pushButton(PlayerButton::Jump);
            player->setPositionY(player->getPositionY() + static_cast<float>(4.0 * grav));
            std::snprintf(logMsg, sizeof(logMsg), "[BUFFER SAVED] Wave corridor hazard deflected! (Ticks: %d)\n", maxTicks);
        } 
        else if (player->m_isShip || player->m_isSwing) {
            // Ship / Swing vector adjustment
            player->m_yVelocity = 5.0 * grav;
            player->setPositionY(player->getPositionY() + static_cast<float>(4.0 * grav));
            std::snprintf(logMsg, sizeof(logMsg), "[BUFFER SAVED] Ship/Swing obstacle bypassed! (Ticks: %d)\n", maxTicks);
        } 
        else {
            // Cube / Ball / Spider / Robot / UFO frame-perfect jump & spike rescue
            player->m_isOnGround = true;
            player->pushButton(PlayerButton::Jump);
            if (player->m_yVelocity * grav < 11.0) {
                player->m_yVelocity = 11.5 * grav;
            }
            player->setPositionY(player->getPositionY() + static_cast<float>(4.0 * grav));
            std::snprintf(logMsg, sizeof(logMsg), "[BUFFER SAVED] 100-Frame-Perfect spike death prevented! (Ticks: %d)\n", maxTicks);
        }

        BufferEngine::sendToCMD(logMsg);
        BufferEngine::internalBypass = false;
    }
};
