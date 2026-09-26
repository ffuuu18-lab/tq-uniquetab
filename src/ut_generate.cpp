// ut_generate.cpp - see ut_generate.h.
#include "ut_generate.h"

#include "gen/generate.h"
#include "ut_config.h"
#include "ut_log.h"
#include "ut_paths.h"

#include <cctype>
#include <cstring>
#include <string>

namespace ut {

namespace {

// The ini value names a file suffix: letters and digits only, up to seven of them, else EN.
std::string textLanguage() {
    std::string lang;
    for (const char* p = g_cfg.textLanguage; *p && lang.size() < 7; ++p) {
        if (!std::isalnum((unsigned char)*p)) { lang.clear(); break; }
        lang += (char)std::toupper((unsigned char)*p);
    }
    if (lang.empty()) {
        if (g_cfg.textLanguage[0]) {
            logW("config: text_language=%s is not a Text_<lang>.arc suffix - using EN",
                 g_cfg.textLanguage);
        }
        lang = "EN";
    }
    return lang;
}

volatile LONG g_generating = 0;   // a generation is running
volatile LONG g_generated = 0;    // generateEnsure has returned

struct StartCtx {
    const char* gameDir;
    const char* modDir;
    const std::string* lang;
};

// Printed before the archives are read, so the log says why the start-up pauses for a few seconds.
void sayStart(const gen::GenReport& report, void* ctx) {
    const StartCtx* c = (const StartCtx*)ctx;
    logI("catalogue: building from the game's database (%s, names from Text_%s.arc) ...",
         report.reason.empty() ? "no reason recorded" : report.reason.c_str(), c->lang->c_str());
    logD("catalogue: game folder \"%s\", output folder \"%s\"", c->gameDir, c->modDir);
    logFlush();
}

}  // namespace

void generateEnsure(HMODULE selfModule) {
    char gameDir[MAX_PATH] = {0};
    char modDir[MAX_PATH] = {0};
    if (!utGameDirA(gameDir, sizeof(gameDir))) {
        logE("catalogue: the game folder could not be resolved from the host exe's own path - "
             "the data files in the mod folder are used as they are");
        InterlockedExchange(&g_generated, 1);
        return;
    }
    utModDirA(selfModule, modDir, sizeof(modDir));
    const std::string lang = textLanguage();

    gen::GenReport report;
    StartCtx ctx = {gameDir, modDir, &lang};
    InterlockedIncrement(&g_generating);
    const bool ok = gen::ensureOutputsGuarded(gameDir, modDir, lang, report, &sayStart, &ctx);
    InterlockedDecrement(&g_generating);
    InterlockedExchange(&g_generated, 1);
    if (ok && report.upToDate) {
        logI("catalogue: up to date (stamp matches) - catalogue.bin and the uniq-*.txt files in "
             "\"%s\" are current (Text_%s)",
             modDir, lang.c_str());
        return;
    }
    for (const std::string& w : report.warnings) logW("catalogue: %s", w.c_str());
    if (ok) {
        logI("catalogue: %s", report.summary.c_str());
    } else {
        logE("catalogue: generation FAILED - %s. The files already in \"%s\" are kept",
             report.error.c_str(), modDir);
    }
}

bool generateBusy() { return InterlockedCompareExchange(&g_generating, 0, 0) != 0; }
bool generateDone() { return InterlockedCompareExchange(&g_generated, 0, 0) != 0; }

}  // namespace ut
