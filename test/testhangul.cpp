/*
 * SPDX-FileCopyrightText: 2024-2024 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#include "testdir.h"
#include "testfrontend_public.h"
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/macros.h>
#include <fcitx-utils/testing.h>
#include <fcitx-config/rawconfig.h>
#include <pango/pangocairo.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/surroundingtext.h>

using namespace fcitx;

void scheduleEvent(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *hangul = instance->addonManager().addon("hangul", true);
        FCITX_ASSERT(hangul);
        auto defaultGroup = instance->inputMethodManager().currentGroup();
        defaultGroup.inputMethodList().clear();
        defaultGroup.inputMethodList().push_back(
            InputMethodGroupItem("keyboard-us"));
        defaultGroup.inputMethodList().push_back(
            InputMethodGroupItem("hangul"));
        defaultGroup.setDefaultInputMethod("");
        instance->inputMethodManager().setGroup(defaultGroup);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        testfrontend->call<ITestFrontend::pushCommitExpectation>("ㅂ");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("ㅃ");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("ㅂ");
        FCITX_ASSERT(instance->inputMethod(ic) == "hangul");

        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("q"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Q"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_Q, KeyState::CapsLock), false));
        instance->deactivate();

        instance->activate();
        for (const char *key : {"d", "l", "f", "F9"}) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(key), false));
        }
        const auto &candidates = ic->inputPanel().candidateList();
        FCITX_ASSERT(candidates && candidates->size() >= 2);
        FCITX_ASSERT(candidates->candidate(0).text().toString() == "一　");
        FCITX_ASSERT(candidates->candidate(0).comment().toString() == "한 일");
        FCITX_ASSERT(candidates->candidate(1).text().toString() == "日　");
        FCITX_ASSERT(candidates->candidate(1).comment().toString() == "날 일");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("日");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("2"), false));
        instance->deactivate();

        RawConfig config;
        config.setValueByPath("WordCommit", "True");
        hangul->setConfig(config);
        instance->activate();
        for (const char *key : {"d", "l", "f", "d", "y", "d", "l", "f", "F9"}) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(key), false));
        }
        const auto &words = ic->inputPanel().candidateList();
        FCITX_ASSERT(words && words->size() == 5);
        FCITX_ASSERT(words->candidate(0).text().toString() == "日曜日　");
        FCITX_ASSERT(words->candidate(0).comment().empty());
        FCITX_ASSERT(words->candidate(1).text().toString() == "日曜　　");
        FCITX_ASSERT(words->candidate(2).text().toString() == "一　　　");

        // Verify actual glyph advances, not just string lengths, at two sizes.
        auto *pango = pango_font_map_create_context(pango_cairo_font_map_get_default());
        for (const char *font : {"Pretendard,Noto Sans CJK KR 12",
                                 "Pretendard,Noto Sans CJK KR 18",
                                 "Pretendard,Pretendard JP 12 #tnum=1,ss05=1",
                                 "Pretendard,Pretendard JP 18 #tnum=1,ss05=1"}) {
            auto *description = pango_font_description_from_string(font);
            pango_context_set_font_description(pango, description);
            pango_font_description_free(description);
            int expectedWidth = -1;
            for (int i = 0; i < words->size(); ++i) {
                auto *layout = pango_layout_new(pango);
                const auto text = words->candidate(i).text().toString();
                pango_layout_set_text(layout, text.c_str(), -1);
                int width;
                pango_layout_get_size(layout, &width, nullptr);
                if (expectedWidth < 0) { expectedWidth = width; }
                FCITX_ASSERT(width == expectedWidth);
                g_object_unref(layout);
            }
        }
        g_object_unref(pango);
        testfrontend->call<ITestFrontend::pushCommitExpectation>("日曜日");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("1"), false));
        instance->deactivate();

        // A phrase-only selection has no gloss: don't pad an empty column.
        instance->activate();
        ic->setCapabilityFlags(ic->capabilityFlags() | CapabilityFlag::SurroundingText);
        ic->surroundingText().setText("일요일", 3, 0);
        ic->updateSurroundingText();
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("F9"), false));
        const auto &selection = ic->inputPanel().candidateList();
        FCITX_ASSERT(selection && selection->size() == 1);
        FCITX_ASSERT(selection->candidate(0).text().toString() == "日曜日");
        FCITX_ASSERT(selection->candidate(0).comment().empty());
        testfrontend->call<ITestFrontend::pushCommitExpectation>("日曜日");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("1"), false));
        instance->deactivate();
    });

    instance->eventDispatcher().schedule([instance]() { instance->exit(); });
}

int main() {
    setupTestingEnvironmentPath(TESTING_BINARY_DIR, {"bin"},
                                {TESTING_BINARY_DIR "/test"});
    char arg0[] = "testhangul";
    char arg1[] = "--disable=all";
    char arg2[] = "--enable=testim,testfrontend,hangul";
    char *argv[] = {arg0, arg1, arg2};
    fcitx::Log::setLogRule("default=5,hangul=5");
    Instance instance(FCITX_ARRAY_SIZE(argv), argv);
    instance.addonManager().registerDefaultLoader(nullptr);
    scheduleEvent(&instance);
    instance.exec();

    return 0;
}
