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
import java.io.File;
import java.io.FileInputStream;
import java.util.HashMap;
import java.util.Locale;

/** Text for the system-info overlay: FPS, process CPU, estimated GPU load and memory. Call sample() from the UI poll; it refreshes about once a second. */
final class SystemInfo {
    private final Context ctx;
    private final ActivityManager am;
    private final long tick = Math.max(1, Os.sysconf(OsConstants._SC_CLK_TCK));
    private final int cores = Math.max(1, Runtime.getRuntime().availableProcessors());
    private final HashMap<String, Long> threads = new HashMap<>();
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
        String f = Double.isNaN(fps) ? "—" : String.format(Locale.US, "%.1f", fps);
        text = String.format(Locale.US, "FPS %s\nCPU %d%%  最忙线程 %d%%\nGPU %d%%%s\n内存 %s  系统剩余 %s", f,
            Math.round(Math.min(100, total / cores * 100)), Math.round(Math.min(100, busiest * 100)),
            Math.round(Math.min(100, Math.max(0, gpu) * 100)), gpuMemBytes >= 0 ? "  显存 " + size(gpuMemBytes) : "",
            size(mi.getTotalPss() * 1024L), size(sys.availMem)) + power();
        return text;
    }

    /** Whole-phone battery power (screen, radios and every app included, not just this game), averaged over the last 3 samples. */
    private String power() {
        BatteryManager bm = (BatteryManager) ctx.getSystemService(Context.BATTERY_SERVICE);
        Intent b = ctx.registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        long cur = bm.getLongProperty(BatteryManager.BATTERY_PROPERTY_CURRENT_NOW);
        if (b == null || cur == Long.MIN_VALUE || cur == 0) return "\n功耗 不可用";
        double ma = Math.abs(cur) >= 20000 ? Math.abs(cur) / 1000.0 : Math.abs(cur);   // vendors report either µA or mA
        double w = ma * b.getIntExtra(BatteryManager.EXTRA_VOLTAGE, 0) / 1e6;
        watts[nWatts++ % watts.length] = w;
        double avg = 0; int n = Math.min(nWatts, watts.length); for (int i = 0; i < n; i++) avg += watts[i]; avg /= n;
        boolean charging = b.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) != 0;
        return String.format(Locale.US, charging ? "\n充电 %.1f W（接电源时不是游戏功耗）" : "\n整机功耗 %.1f W", avg);
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
