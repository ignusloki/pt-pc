# P.T. PC Port 1.1.3

- Synced with LoreanXavier’s 1.1.2. It improves traced reflections, third-person gun poses and sidesteps, Armory skins, ammo readouts and arrow-key rebinding. [Upstream changelog](https://github.com/LoreanXavier/pt-pc/releases/tag/v1.1.2)
- Fixed an extra frame-limiter delay and reduced redundant rendering work to smooth weapon switching and reloading in *Survive the Night*.
- Reduced peak memory when starting *Survive the Night* on iOS. Weapon skins now follow the device texture budget, and temporary skin-baking buffers are released after use.

Downloads cover Windows x64 (portable ZIP), macOS on Apple silicon and Intel (portable ZIP, DMG or Setup), iOS/iPadOS 17+ (IPA for sideloading), and Android arm64 (signed APK). On Windows, extract the ZIP, run `pt.exe`, and select your existing game folder.

Both Original and Enhanced HDR were playtested on Apple silicon in 1.1.3-rc1. Automated Apple silicon playtesting also covered entering *Survive the Night*, switching all five guns, firing and reloading. Full gameplay validation on Windows, Intel Mac, iOS and Android is pending for 1.1.3.

[Built on Hallway Engine by LoreanXavier](https://github.com/LoreanXavier/pt-pc).
