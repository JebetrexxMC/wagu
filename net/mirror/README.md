# Mirror (Unity)

`MPNetWireManager.cs` is a [Mirror](https://github.com/JebetrexxMC/Mirror) `NetworkManager` for Unity companion games and tools: `StartClientBestWire()` reads the NetWire status file, picks the best wire of a group (ping + loss) and joins it. It sets the port on any Mirror `PortTransport` (KCP, Telepathy, ...).

It is not compiled in CI (Unity is not on the runners). Copy it into `Assets/` of a project that has Mirror installed.
