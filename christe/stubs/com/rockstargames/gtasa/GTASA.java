package com.rockstargames.gtasa;

// Compile-time stand-in only (build.sh puts it on javac's classpath, it is NOT in classes.dex).
// At run time com.christe.mp.Game extends the real GTASA activity of the installed AML GTA SA,
// loaded from that APK by ChristeFactory / ChristeApp. Nothing of Rockstar's is shipped in Christe.
public class GTASA extends android.app.Activity {
    public String FileGetArchiveName(int type) { return null; }
}
