package com.yuriaaaaa.marvelnemesis;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Environment;

import java.io.File;
import java.util.Locale;

/** Where the player's game is (a disc image, or the extracted disc) and where saves and logs go. */
final class Paths {
    private Paths() {}

    static SharedPreferences prefs(Context c) {
        return c.getSharedPreferences("marvelnemesis", Context.MODE_PRIVATE);
    }

    /** Internal storage/MarvelNemesis: the default game folder, saves (hdd) and debug logs. */
    static File dataDir() {
        return new File(Environment.getExternalStorageDirectory(), "MarvelNemesis");
    }

    static File iso(File d) {
        File[] kids = d == null ? null : d.listFiles();
        if (kids == null) return null;
        for (File k : kids)
            if (k.isFile() && k.getName().toLowerCase(Locale.US).endsWith(".iso")) return k;
        return null;
    }

    static boolean extracted(File d) {
        return d != null && new File(d, "default.xbe").isFile() && hasDir(d, "Bundles");
    }

    static boolean looksLikeGame(File d) {
        return extracted(d) || iso(d) != null;
    }

    /** The saved folder, else Internal storage/MarvelNemesis, else a copy of the game found under Download. */
    static String gameDir(Context c) {
        String saved = prefs(c).getString("dir", null);
        if (saved != null && looksLikeGame(new File(saved))) return saved;
        File def = dataDir();
        if (looksLikeGame(def)) return def.getAbsolutePath();
        File dl = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
        File found = search(dl, 2);
        if (found != null) return found.getAbsolutePath();
        return saved != null ? saved : def.getAbsolutePath();
    }

    private static File search(File d, int depth) {
        if (d == null || !d.isDirectory()) return null;
        if (looksLikeGame(d)) return d;
        if (depth == 0) return null;
        File[] kids = d.listFiles();
        if (kids == null) return null;
        for (File k : kids) {
            if (!k.isDirectory()) continue;
            File f = search(k, depth - 1);
            if (f != null) return f;
        }
        return null;
    }

    /** a folder of the disc next to default.xbe, whatever its case (the game opens it ignoring case) */
    private static boolean hasDir(File d, String name) {
        File[] list = d.listFiles();
        if (list == null) return false;
        for (File f : list) if (f.isDirectory() && f.getName().equalsIgnoreCase(name)) return true;
        return false;
    }
}
