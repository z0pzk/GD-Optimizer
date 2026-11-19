cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(GDOptimizer VERSION 1.0.0)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${SOURCES})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

setup_geode_mod(${PROJECT_NAME})
3. src/main.cpp
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/CCParticleSystem.hpp>
#include <Geode/modify/CCParticleSystemQuad.hpp>

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
        bool autoLdm = true;
        bool stripHighDetailDeco = true;
        bool disableParticles = true;
        bool disableGlow = true;
        bool disableDeathEffect = false;
        bool hideBackground = false;
    };

    OptConfig g_cfg;

    void applyWindowsPriority(bool high) {
    #ifdef GEODE_IS_WINDOWS
        SetPriorityClass(
            GetCurrentProcess(),
            high ? HIGH_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS
        );
    #else
        (void)high;
    #endif
    }
}

$on_mod(Loaded) {
    auto* mod = Mod::get();

    g_cfg.autoLdm = mod->getSettingValue<bool>("auto-ldm");
    g_cfg.stripHighDetailDeco = mod->getSettingValue<bool>("strip-high-detail-deco");
    g_cfg.disableParticles = mod->getSettingValue<bool>("disable-particles");
    g_cfg.disableGlow = mod->getSettingValue<bool>("disable-glow");
    g_cfg.disableDeathEffect = mod->getSettingValue<bool>("disable-death-effect");
    g_cfg.hideBackground = mod->getSettingValue<bool>("hide-background");

#ifdef GEODE_IS_WINDOWS
    applyWindowsPriority(mod->getSettingValue<bool>("high-cpu-priority"));
    listenForSettingChanges<bool>("high-cpu-priority", [](bool value) {
        applyWindowsPriority(value);
    });
#endif

    listenForSettingChanges<bool>("auto-ldm", [](bool value) {
        g_cfg.autoLdm = value;
    });
    listenForSettingChanges<bool>("strip-high-detail-deco", [](bool value) {
        g_cfg.stripHighDetailDeco = value;
    });
    listenForSettingChanges<bool>("disable-particles", [](bool value) {
        g_cfg.disableParticles = value;
    });
    listenForSettingChanges<bool>("disable-glow", [](bool value) {
        g_cfg.disableGlow = value;
    });
    listenForSettingChanges<bool>("disable-death-effect", [](bool value) {
        g_cfg.disableDeathEffect = value;
    });
    listenForSettingChanges<bool>("hide-background", [](bool value) {
        g_cfg.hideBackground = value;
    });
}

class $modify(OptPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (g_cfg.autoLdm && level) {
            level->m_lowDetailModeToggled = true;
        }
        return PlayLayer::init(level, useReplay, dontCreateObjects);
    }

    void addObject(GameObject* obj) {
        if (obj) {
            if (g_cfg.stripHighDetailDeco &&
                obj->m_isHighDetail &&
                obj->m_objectType == GameObjectType::Decoration) {
                return;
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

        if (g_cfg.hideBackground && m_background) {
            m_background->setVisible(false);
        }
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
