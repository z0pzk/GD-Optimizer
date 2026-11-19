#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/CCSpriteBatchNode.hpp>
#include <Geode/modify/CCParticleSystem.hpp>
#include <Geode/modify/CCParticleSystemQuad.hpp>
#include <Geode/modify/CCMotionStreak.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <unordered_map>

#ifdef GEODE_IS_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace geode::prelude;

namespace {
    struct OptConfig {
        bool adaptiveVisThrottle = true;
        int64_t throttleThresholdMs = 16;
        int64_t maxVisSkip = 3;

        bool spatialDecoLimit = true;
        int64_t maxDecoPerCell = 3;
        int64_t gridCellSize = 30;
        int64_t maxTotalDeco = 15000;
        bool cullTinyDeco = true;

        bool skipAdditiveBatches = true;
        bool autoLdm = true;
        bool stripHighDetailDeco = true;
        bool disableGlow = true;
        bool disableParticles = true;
        bool disableTrails = false;
        bool disableDeathEffect = false;
        bool hideBackground = false;
    };

    OptConfig g_cfg;

#ifdef GEODE_IS_WINDOWS
    bool g_timerBoostActive = false;

    void applyWindowsBoost(bool enable) {
        SetPriorityClass(
            GetCurrentProcess(),
            enable ? HIGH_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS
        );
        SetThreadPriority(
            GetCurrentThread(),
            enable ? THREAD_PRIORITY_HIGHEST : THREAD_PRIORITY_NORMAL
        );

        if (HMODULE winmm = LoadLibraryA("winmm.dll")) {
            using TimePeriodFn = UINT(WINAPI*)(UINT);
            if (enable && !g_timerBoostActive) {
                if (auto fn = reinterpret_cast<TimePeriodFn>(GetProcAddress(winmm, "timeBeginPeriod"))) {
                    if (fn(1) == 0) {
                        g_timerBoostActive = true;
                    }
                }
            } else if (!enable && g_timerBoostActive) {
                if (auto fn = reinterpret_cast<TimePeriodFn>(GetProcAddress(winmm, "timeEndPeriod"))) {
                    fn(1);
                    g_timerBoostActive = false;
                }
            }
        }

        if (HMODULE kernel32 = GetModuleHandleA("kernel32.dll")) {
            struct PowerThrottlingState {
                ULONG Version;
                ULONG ControlMask;
                ULONG StateMask;
            };
            using SetProcInfoFn = BOOL(WINAPI*)(HANDLE, int, LPVOID, DWORD);
            if (auto fn = reinterpret_cast<SetProcInfoFn>(GetProcAddress(kernel32, "SetProcessInformation"))) {
                PowerThrottlingState state{};
                state.Version = 1;
                state.ControlMask = 1;
                state.StateMask = enable ? 0 : 1;
                fn(GetCurrentProcess(), 4, &state, sizeof(state));
            }
        }
    }
#endif
}

$on_mod(Loaded) {
    auto* mod = Mod::get();

    g_cfg.adaptiveVisThrottle = mod->getSettingValue<bool>("adaptive-vis-throttle");
    g_cfg.throttleThresholdMs = mod->getSettingValue<int64_t>("throttle-threshold-ms");
    g_cfg.maxVisSkip = mod->getSettingValue<int64_t>("max-vis-skip");

    g_cfg.spatialDecoLimit = mod->getSettingValue<bool>("spatial-deco-limit");
    g_cfg.maxDecoPerCell = mod->getSettingValue<int64_t>("max-deco-per-cell");
    g_cfg.gridCellSize = mod->getSettingValue<int64_t>("grid-cell-size");
    g_cfg.maxTotalDeco = mod->getSettingValue<int64_t>("max-total-deco");
    g_cfg.cullTinyDeco = mod->getSettingValue<bool>("cull-tiny-deco");

    g_cfg.skipAdditiveBatches = mod->getSettingValue<bool>("skip-additive-batches");
    g_cfg.autoLdm = mod->getSettingValue<bool>("auto-ldm");
    g_cfg.stripHighDetailDeco = mod->getSettingValue<bool>("strip-high-detail-deco");
    g_cfg.disableGlow = mod->getSettingValue<bool>("disable-glow");
    g_cfg.disableParticles = mod->getSettingValue<bool>("disable-particles");
    g_cfg.disableTrails = mod->getSettingValue<bool>("disable-trails");
    g_cfg.disableDeathEffect = mod->getSettingValue<bool>("disable-death-effect");
    g_cfg.hideBackground = mod->getSettingValue<bool>("hide-background");

#ifdef GEODE_IS_WINDOWS
    applyWindowsBoost(mod->getSettingValue<bool>("win-scheduler-boost"));
    listenForSettingChanges<bool>("win-scheduler-boost", [](bool value) {
        applyWindowsBoost(value);
    });
#endif

    listenForSettingChanges<bool>("adaptive-vis-throttle", [](bool v) { g_cfg.adaptiveVisThrottle = v; });
    listenForSettingChanges<int64_t>("throttle-threshold-ms", [](int64_t v) { g_cfg.throttleThresholdMs = v; });
    listenForSettingChanges<int64_t>("max-vis-skip", [](int64_t v) { g_cfg.maxVisSkip = v; });

    listenForSettingChanges<bool>("spatial-deco-limit", [](bool v) { g_cfg.spatialDecoLimit = v; });
    listenForSettingChanges<int64_t>("max-deco-per-cell", [](int64_t v) { g_cfg.maxDecoPerCell = v; });
    listenForSettingChanges<int64_t>("grid-cell-size", [](int64_t v) { g_cfg.gridCellSize = v; });
    listenForSettingChanges<int64_t>("max-total-deco", [](int64_t v) { g_cfg.maxTotalDeco = v; });
    listenForSettingChanges<bool>("cull-tiny-deco", [](bool v) { g_cfg.cullTinyDeco = v; });

    listenForSettingChanges<bool>("skip-additive-batches", [](bool v) { g_cfg.skipAdditiveBatches = v; });
    listenForSettingChanges<bool>("auto-ldm", [](bool v) { g_cfg.autoLdm = v; });
    listenForSettingChanges<bool>("strip-high-detail-deco", [](bool v) { g_cfg.stripHighDetailDeco = v; });
    listenForSettingChanges<bool>("disable-glow", [](bool v) { g_cfg.disableGlow = v; });
    listenForSettingChanges<bool>("disable-particles", [](bool v) { g_cfg.disableParticles = v; });
    listenForSettingChanges<bool>("disable-trails", [](bool v) { g_cfg.disableTrails = v; });
    listenForSettingChanges<bool>("disable-death-effect", [](bool v) { g_cfg.disableDeathEffect = v; });
    listenForSettingChanges<bool>("hide-background", [](bool v) { g_cfg.hideBackground = v; });
}

class $modify(OptPlayLayer, PlayLayer) {
    struct Fields {
        std::unordered_map<uint64_t, int> decoCells;
        int64_t totalDecoLoaded = 0;

        int skipCounter = 0;
        int warmupFrames = 12;
        float accumDt = 0.f;
        double smoothedFrameMs = 16.6;
        std::chrono::steady_clock::time_point lastTime = std::chrono::steady_clock::now();
    };

    void resetWarmup() {
        m_fields->warmupFrames = 12;
        m_fields->accumDt = 0.f;
        m_fields->skipCounter = 0;
        m_fields->lastTime = std::chrono::steady_clock::now();
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        m_fields->decoCells.clear();
        m_fields->totalDecoLoaded = 0;
        resetWarmup();

        if (g_cfg.autoLdm && level) {
            level->m_lowDetailModeToggled = true;
        }
        return PlayLayer::init(level, useReplay, dontCreateObjects);
    }

    void resetLevel() {
        resetWarmup();
        PlayLayer::resetLevel();
    }

    void updateVisibility(float dt) {
        if (!g_cfg.adaptiveVisThrottle) {
            PlayLayer::updateVisibility(dt);
            return;
        }

        auto now = std::chrono::steady_clock::now();
        double frameMs = std::chrono::duration<double, std::milli>(now - m_fields->lastTime).count();
        m_fields->lastTime = now;

        if (frameMs > 0.1 && frameMs < 200.0) {
            m_fields->smoothedFrameMs = m_fields->smoothedFrameMs * 0.85 + frameMs * 0.15;
        }

        if (m_fields->warmupFrames > 0) {
            m_fields->warmupFrames--;
            PlayLayer::updateVisibility(dt);
            return;
        }

        int stride = 1;
        int maxSkip = std::max(1, static_cast<int>(g_cfg.maxVisSkip));
        double threshold = static_cast<double>(g_cfg.throttleThresholdMs);

        if (m_fields->smoothedFrameMs > threshold * 1.5) {
            stride = maxSkip;
        } else if (m_fields->smoothedFrameMs > threshold) {
            stride = std::min(maxSkip, 2);
        }

        m_fields->accumDt += dt;
        m_fields->skipCounter++;

        if (m_fields->skipCounter >= stride) {
            float totalDt = m_fields->accumDt;
            m_fields->accumDt = 0.f;
            m_fields->skipCounter = 0;
            PlayLayer::updateVisibility(totalDt);
        }
    }

    void addObject(GameObject* obj) {
        if (obj) {
            if (obj->m_objectType == GameObjectType::Decoration) {
                if (g_cfg.stripHighDetailDeco && obj->m_isHighDetail) {
                    return;
                }

                if (g_cfg.cullTinyDeco) {
                    float sx = std::fabs(obj->getScaleX());
                    float sy = std::fabs(obj->getScaleY());
                    if (sx < 0.25f && sy < 0.25f) {
                        return;
                    }
                }

                if (g_cfg.maxTotalDeco > 0 && m_fields->totalDecoLoaded >= g_cfg.maxTotalDeco) {
                    return;
                }

                if (g_cfg.spatialDecoLimit) {
                    int cellSize = std::max(8, static_cast<int>(g_cfg.gridCellSize));
                    auto pos = obj->getPosition();
                    int gx = static_cast<int>(std::floor(pos.x / static_cast<float>(cellSize)));
                    int gy = static_cast<int>(std::floor(pos.y / static_cast<float>(cellSize)));
                    uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(gx)) << 32) |
                                   static_cast<uint64_t>(static_cast<uint32_t>(gy));

                    int& count = m_fields->decoCells[key];
                    if (count >= g_cfg.maxDecoPerCell) {
                        return;
                    }
                    count++;
                }

                m_fields->totalDecoLoaded++;
            }

            if (g_cfg.disableGlow) {
            obj->m_hasNoGlow = true;
            if (obj->m_glowSprite) {
            obj->m_glowSprite->setVisible(false);
}
}
}
PlayLayer::addObject(obj);
}
void setupHasCompleted() {
PlayLayer::setupHasCompleted();
std::unordered_map<uint64_t, int>().swap(m_fields->decoCells);
if (g_cfg.hideBackground && m_background) {
m_background->setVisible(false);
}
}
};
class $modify(OptSpriteBatchNode, CCSpriteBatchNode) {
void draw() {
if (g_cfg.skipAdditiveBatches && PlayLayer::get() != nullptr) {
if (this->getBlendFunc().dst == GL_ONE) {
return;
}
}
CCSpriteBatchNode::draw();
}
};
class $modify(OptPlayerObject, PlayerObject) {
void playDeathEffect() {
if (g_cfg.disableDeathEffect) {
return;
}
PlayerObject::playDeathEffect();
}
};
class $modify(OptParticleSystem, CCParticleSystem) {
void update(float dt) {
if (g_cfg.disableParticles && this->getEmissionRate() > 0.f) {
this->setEmissionRate(0.f);
}
CCParticleSystem::update(dt);
}
};
class $modify(OptParticleSystemQuad, CCParticleSystemQuad) {
void draw() {
if (g_cfg.disableParticles) {
return;
}
CCParticleSystemQuad::draw();
}
};
class $modify(OptMotionStreak, CCMotionStreak) {
void update(float delta) {
if (g_cfg.disableTrails) {
return;
}
CCMotionStreak::update(delta);
}
void draw() {
if (g_cfg.disableTrails) {
return;
}
CCMotionStreak::draw();
}
};
