package org.d2r.binaryprobe;

import android.app.ActivityManager;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.BatteryManager;
import android.os.Debug;
import android.os.SystemClock;
import android.system.Os;
import android.system.OsConstants;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.FileInputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;

/** Text for the system-info overlay: FPS, process CPU, estimated GPU load, memory, power and temperatures. Call sample() from the UI poll; it refreshes about once a second. */
final class SystemInfo {
    private final Context ctx;
    private final ActivityManager am;
    private final long tick = Math.max(1, Os.sysconf(OsConstants._SC_CLK_TCK));
    private final int cores = Math.max(1, Runtime.getRuntime().availableProcessors());
    private final HashMap<String, Long> threads = new HashMap<>();
    private final List<File> cpuZones = zones("cpu-"), gpuZones = zones("gpuss"), skinZones = zones("shell_");
    private long wall, gpuNs;
    private final double[] watts = new double[3]; private int nWatts;
    private String text = "";

    SystemInfo(Context c) { ctx = c; am = (ActivityManager) c.getSystemService(Context.ACTIVITY_SERVICE); }

    void reset() { wall = 0; threads.clear(); text = ""; nWatts = 0; }

    /** @param fps NaN until measured; @param gpuBusyNs cumulative GPU time from the renderer; @param gpuMemBytes renderer allocations (or <0) */
    String sample(double fps, long gpuBusyNs, long gpuMemBytes) {
        long now = SystemClock.elapsedRealtime();
        if (wall != 0 && now - wall < 1000 && !text.isEmpty()) return text;
        double dt = (now - wall) / 1000.0; boolean first = wall == 0;
        // CPU: ticks of every thread of this process since the last sample
        double total = 0, busiest = 0;
        String[] ids = new File("/proc/self/task").list();
        HashMap<String, Long> next = new HashMap<>();
        if (ids != null) for (String id : ids) {
            long t = ticks(id); if (t < 0) continue;
            next.put(id, t); Long prev = threads.get(id);
            if (prev != null && !first) { double d = (t - prev) / (double) tick / dt; total += d; busiest = Math.max(busiest, d); }
        }
        threads.clear(); threads.putAll(next);
        double gpu = first ? 0 : (gpuBusyNs - gpuNs) / 1e9 / dt;
        gpuNs = gpuBusyNs; wall = now;
        Debug.MemoryInfo mi = new Debug.MemoryInfo(); Debug.getMemoryInfo(mi);
        ActivityManager.MemoryInfo sys = new ActivityManager.MemoryInfo(); am.getMemoryInfo(sys);
        StringBuilder t = new StringBuilder();
        row(t, "FPS", Double.isNaN(fps) ? "—" : String.format(Locale.US, "%.1f", fps));
        row(t, "CPU", Math.round(Math.min(100, total / cores * 100)) + "%");
        row(t, "Top thread", Math.round(Math.min(100, busiest * 100)) + "%");
        row(t, "GPU", Math.round(Math.min(100, Math.max(0, gpu) * 100)) + "%");
        if (gpuMemBytes >= 0) row(t, "VRAM", size(gpuMemBytes));
        row(t, "RAM", size(mi.getTotalPss() * 1024L));
        row(t, "Free RAM", size(sys.availMem));
        power(t);
        temp(t, "CPU temp", cpuZones); temp(t, "GPU temp", gpuZones); temp(t, "Skin temp", skinZones);
        text = t.toString().trim();
        return text;
    }

    private static void row(StringBuilder t, String name, String value) {
        t.append(name).append('\t').append(value).append('\n');   // InfoView puts the two ends flush left and right
    }

    /** Whole-phone battery power (screen, radios and every app included, not just this game), averaged over the last 3 samples; also the battery temperature. */
    private void power(StringBuilder t) {
        BatteryManager bm = (BatteryManager) ctx.getSystemService(Context.BATTERY_SERVICE);
        Intent b = ctx.registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        long cur = bm.getLongProperty(BatteryManager.BATTERY_PROPERTY_CURRENT_NOW);
        if (b != null && cur != Long.MIN_VALUE && cur != 0) {
            double ma = Math.abs(cur) >= 20000 ? Math.abs(cur) / 1000.0 : Math.abs(cur);   // vendors report either µA or mA
            watts[nWatts++ % watts.length] = ma * b.getIntExtra(BatteryManager.EXTRA_VOLTAGE, 0) / 1e6;
            double avg = 0; int n = Math.min(nWatts, watts.length); for (int i = 0; i < n; i++) avg += watts[i]; avg /= n;
            row(t, b.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) != 0 ? "Charging" : "Power", String.format(Locale.US, "%.1f W", avg));   // charging: not the game's draw
        }
        if (b != null && b.hasExtra(BatteryManager.EXTRA_TEMPERATURE)) row(t, "Battery", String.format(Locale.US, "%.0f°C", b.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, 0) / 10.0));
    }

    /** Hottest readable thermal zone of a group, in °C; the row is omitted when none can be read. */
    private static void temp(StringBuilder t, String name, List<File> zones) {
        double max = Double.NaN;
        for (File f : zones) {
            try (BufferedReader r = new BufferedReader(new FileReader(f))) {
                double v = Double.parseDouble(r.readLine().trim()); if (Math.abs(v) >= 1000) v /= 1000;
                if (Double.isNaN(max) || v > max) max = v;
            } catch (Exception e) { /* unreadable zone */ }
        }
        if (!Double.isNaN(max)) row(t, name, String.format(Locale.US, "%.0f°C", max));
    }

    private static List<File> zones(String prefix) {
        ArrayList<File> out = new ArrayList<>();
        File[] all = new File("/sys/class/thermal").listFiles();
        if (all != null) for (File z : all) {
            if (!z.getName().startsWith("thermal_zone")) continue;
            try (BufferedReader r = new BufferedReader(new FileReader(new File(z, "type")))) {
                if (r.readLine().trim().startsWith(prefix)) out.add(new File(z, "temp"));
            } catch (Exception e) { /* skip */ }
        }
        return out;
    }

    private static long ticks(String tid) {
        try (FileInputStream in = new FileInputStream("/proc/self/task/" + tid + "/stat")) {
            byte[] b = new byte[512]; String s = new String(b, 0, Math.max(0, in.read(b)));
            String[] f = s.substring(s.lastIndexOf(')') + 2).split(" ");   // f[0] is field 3; utime/stime are fields 14/15
            return Long.parseLong(f[11]) + Long.parseLong(f[12]);
        } catch (Exception e) { return -1; }
    }

    private static String size(long bytes) { return bytes >= 1L << 30 ? String.format(Locale.US, "%.1fG", bytes / 1073741824.0) : (bytes >> 20) + "M"; }
}
