package ru.yandex.devtools.util;

import java.lang.management.GarbageCollectorMXBean;
import java.lang.management.ManagementFactory;
import java.lang.management.MemoryPoolMXBean;
import java.lang.management.MemoryUsage;
import java.lang.management.OperatingSystemMXBean;
import java.lang.management.ThreadMXBean;

public class PerformanceMetrics {
    private long garbageCollections;
    private long garbageCollectionTime;

    private long peakMemoryUsage;

    private int peakThreadCount;
    private long threadCPUTime;
    private long totalCPUTime;

    public long getGarbageCollections() {
        return garbageCollections;
    }

    public long getGarbageCollectionTime() {
        return garbageCollectionTime;
    }

    public long getPeakMemoryUsage() {
        return peakMemoryUsage;
    }

    public int getPeakThreadCount() {
        return peakThreadCount;
    }

    public long getThreadCPUTime() {
        return threadCPUTime;
    }

    public long getTotalCPUTime() {
        return totalCPUTime;
    }

    public void fillGarbageCollectionMetrics() {
        long totalGarbageCollections = 0;
        long totalGarbageCollectionTime = 0;

        for (GarbageCollectorMXBean gc : ManagementFactory.getGarbageCollectorMXBeans()) {
            long count = gc.getCollectionCount();

            if (count >= 0) {
                totalGarbageCollections += count;
            }

            long time = gc.getCollectionTime();

            if (time >= 0) {
                totalGarbageCollectionTime += time;
            }
        }
        garbageCollections = totalGarbageCollections;
        garbageCollectionTime = totalGarbageCollectionTime;

    }

    public void fillMemoryUsageMetrics() {
        long totalMemoryUsage = 0;

        for (MemoryPoolMXBean pool : ManagementFactory.getMemoryPoolMXBeans()) {
            MemoryUsage peak = pool.getPeakUsage();
            totalMemoryUsage += peak.getUsed();
        }

        peakMemoryUsage = totalMemoryUsage;
    }

    public static void resetPeakMemoryUsage() {
        for (MemoryPoolMXBean pool : ManagementFactory.getMemoryPoolMXBeans()) {
            pool.resetPeakUsage();
        }
    }

    public void fillTotalCpuUsageMetrics() {
        OperatingSystemMXBean osBean = ManagementFactory.getOperatingSystemMXBean();
        if (osBean instanceof com.sun.management.OperatingSystemMXBean) {
            com.sun.management.OperatingSystemMXBean sunOsBean = (com.sun.management.OperatingSystemMXBean) osBean;
            long time = sunOsBean.getProcessCpuTime();
            if (time != -1) {
                totalCPUTime = time;
            }
        }
    }

    public void fillThreadMetrics() {
        ThreadMXBean threadBean = ManagementFactory.getThreadMXBean();
        long totalThreadCPUTime = 0;
        for (long threadId : threadBean.getAllThreadIds()) {
            totalThreadCPUTime += threadBean.getThreadCpuTime(threadId);
        }
        threadCPUTime = totalThreadCPUTime;
        peakThreadCount = threadBean.getPeakThreadCount();
    }

    public static void resetPeakThreadCount() {
        ThreadMXBean threadBean = ManagementFactory.getThreadMXBean();
        threadBean.resetPeakThreadCount();

    }

    public void updatePeakMemoryUsage(long currentPeakMemoryUsage) {
        if (currentPeakMemoryUsage > peakMemoryUsage) {
            peakMemoryUsage = currentPeakMemoryUsage;
        }
    }

    public void updatePeakThreadCount(int currentPeakThreadCount) {
        if (currentPeakThreadCount > peakThreadCount) {
            peakThreadCount = currentPeakThreadCount;
        }
    }
}
