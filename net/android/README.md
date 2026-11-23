# NetWire for Android / Kotlin apps

Read `wires.json` (the netwire-status branch) from Kotlin and get the best wire of a group, for example the fastest crossplay path. Two versions of the same thing:

- `NetWireFuel.kt`: [Fuel](https://github.com/JebetrexxMC/fuel), with coroutines.
- `NetWireFan.kt`: [Fast Android Networking](https://github.com/JebetrexxMC/Fast-Android-Networking), with callbacks.

These are for the Java / Kotlin apps (Moquil, MP Voice, a companion app). The game mod itself is native C++, so it doesn't use them. It pings the 4 regions itself (docs/regions.txt).

They are copy-in source files, not a Gradle module, and aren't compiled in CI.
