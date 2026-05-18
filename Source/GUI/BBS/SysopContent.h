#pragma once
#include <juce_core/juce_core.h>
#include <array>

namespace bombo
{

struct SysopVoice
{
    const char* name;
    // 3-5 MOTDs; one is picked randomly per BBS open.
    std::array<const char*, 5> motds;
    int motdCount;  // valid entries in motds[]
    const char* scrollerLine;
};

// 7 voices. Indices 0-2 available from level 0; 3-6 unlock at levels 1-4.
inline constexpr SysopVoice kSysops[] =
{
    {   // 0 — Future Crew
        "FUTURE CREW",
        { "WELCOME LAMER · DOWNLOAD AT YOUR OWN RISK · SECOND REALITY VIBES TODAY",
          "GREETINGS FROM THE CREW · MUSIC BY PURPLE MOTION · KICKS LOADED",
          "PC DEMO SCENE IS NOT DEAD · NEITHER ARE YOUR DRUMS",
          "CONNECT 2400 · SECOND REALITY STILL HOLDS UP · TRUST THE PROCESS",
          nullptr },
        4,
        "FUTURE CREW PRESENTS: HYPERFOCUS BBS · THE GREATEST KICK ROM ARCHIVE IN THE KNOWN GALAXY · GREETINGS TO ALL SCENE HEADS ·"
    },
    {   // 1 — Spaceballs
        "SPACEBALLS",
        { "NINE FINGERS WAS HERE · HARDCORE KICKS ONLY · WHO SAID AMIGA WAS DEAD",
          "PROTRACKER FOREVER · WAREHOUSE KICKS LOADED · SPACEBALLS SALUTES YOU",
          "AMIGA 1200 OR NOTHING · THESE KICKS ARE HAND-CRAFTED",
          nullptr, nullptr },
        3,
        "SPACEBALLS · NINE FINGERS IS STILL IN THE BUILDING · THE AMIGA NEVER DIES · GREETINGS TO ALL COPPER LOVERS ·"
    },
    {   // 2 — TRSI
        "TRSI",
        { "RELEASE NOTES: PURE FILTH KICKS · NO PROTECTION SCHEMES THIS RELEASE",
          "TRAINED BY TRSI · THESE KICKS REQUIRE NO SERIAL · FREE AS IN FREEDOM",
          "FIRST RELEASE OF THE WEEK · THE COMPETITION IS SLEEPING",
          nullptr, nullptr },
        3,
        "TRISTAR RED SECTOR INC · ELITE KICK DISTRIBUTION SINCE 1989 · THIS RELEASE IS UNPROTECTED · SPREAD THE WORD ·"
    },
    {   // 3 — Razor 1911 (unlocks at L1)
        "RAZOR 1911",
        { "EVEN FREE STUFF NEEDS A NFO · GREETZ TO THE CRACKERS · SINCE 1985",
          "NO INTRO · NO APOLOGY · JUST KICKS · RAZOR APPROVED",
          nullptr, nullptr, nullptr },
        2,
        "RAZOR 1911 · RELEASING SINCE 1985 · NFO INSIDE · GREETINGS TO ALL WAREZ HEADS ·"
    },
    {   // 4 — Fairlight (unlocks at L2)
        "FAIRLIGHT",
        { "WEEKEND DEMOPARTY MODE · WAREHOUSE KICKS LOADED · WE BROUGHT SNACKS",
          "PARTY REPORT: ALL NIGHT SESSION · FAIRLIGHT IN THE HOUSE",
          nullptr, nullptr, nullptr },
        2,
        "FAIRLIGHT · DEMOPARTY ATMOSPHERE GUARANTEED · WAREHOUSE KICKS UNTIL DAWN ·"
    },
    {   // 5 — Triton (unlocks at L3)
        "TRITON",
        { "GREETZ TO THE COMP.SYS.AMIGA HEADS · OCTAMED FOREVER · CRYSTAL DREAM ERA",
          "AMIGA TRACKER CULTURE LIVES HERE · IFF SAMPLES ONLY",
          nullptr, nullptr, nullptr },
        2,
        "TRITON · CRYSTAL DREAM IS ETERNAL · OCTAMED SESSIONS EVERY WEEKEND · AMIGA HEADS REPRESENT ·"
    },
    {   // 6 — Loonies / Conspiracy (unlocks at L4)
        "LOONIES",
        { "4 KILOBYTES OF KICK ENERGY · YOU CAN DO BETTER · ASSEMBLY DEADLINE TONIGHT",
          "4K OR BUST · COMPO ENTRY ACCEPTED · WE LIKE IT TIGHT",
          "CLASSIFIED ARCHIVE NOW ACCESSIBLE · CLEARANCE LEVEL 4 CONFIRMED",
          nullptr, nullptr },
        3,
        "LOONIES / CONSPIRACY · 4K INTRO PURISTS · ASSEMBLY DEMOPARTY FOREVER · COME AT US ·"
    },
};
inline constexpr int kSysopCount = static_cast<int>(sizeof(kSysops) / sizeof(kSysops[0]));

// Returns a random MOTD string for the given sysop index.
inline juce::String pickMotd(int sysopIndex, juce::Random& rng)
{
    const auto& v = kSysops[sysopIndex];
    return v.motds[rng.nextInt(v.motdCount)];
}

} // namespace bombo
