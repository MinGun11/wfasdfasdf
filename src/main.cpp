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
    int masterStage = 3; // 3 = VORTEX (16 Ticks)
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

// Intercept raw player input and seed the predictive buffer countdown
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
            
            char logBuf[128];
            std::snprintf(logBuf, sizeof(logBuf), "[BUFFER] Input registered! Buffer window set to %d ticks.\n", ticks);
            BufferEngine::sendToCMD(logBuf);
        }

        return PlayerObject::pushButton(btn);
    }
};

// Sub-tick buffer execution engine (Processes clicks smoothly across physics frames)
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

                // For Wave / Ship / Swing: Maintain continuous steering assist
                if (p->m_isDart || p->m_isShip || p->m_isSwing) {
                    p->pushButton(PlayerButton::Jump);
                } 
                // For Cube / Orbs / Spider / Ball / Robot: Pre-emptively pulse jump & check orbs
                else {
                    if (p->m_isOnGround) {
                        p->pushButton(PlayerButton::Jump);
                        pressBuf = 0; // Consumed on successful ground hit
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

// Level reset cleanup
class $modify(BufferPlayLayerHook, PlayLayer) {
    void resetLevel() {
        BufferEngine::pressBufferP1 = 0;
        BufferEngine::pressBufferP2 = 0;
        PlayLayer::resetLevel();
    }
};
