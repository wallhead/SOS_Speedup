# Seasons of Skyrim Startup Fix

This patch targets the Seasons of Skyrim v1.9.1 SKSE plugin on Skyrim AE 1.6.1170.
It fixes winter-cache reuse, speeds up the static-model rebuild, and invalidates
the cache when active plugin files change. The cache records empty categories
and validates its contents and FormIDs before reuse. The first launch builds the new cache.

The original Seasons of Skyrim mod by powerofthree supplies the ESP and scripts.
Install the rebuilt DLL as an override for that mod. [Upstream v1.9.1](https://github.com/powerof3/SeasonsOfSkyrim/tree/v1.9.1)

See the [build instructions](BUILD-TRUEAE.md) for the pinned CommonLibSSE submodule
and Visual Studio setup. The [original v2 source ZIP](SeasonsOfSkyrim-1.9.1-TRUEAE-Startup-Fix-Source.zip)
contains the earlier cache format with CommonLibSSE included.

The source retains the upstream [GNU GPL v3 license](LICENSE).
