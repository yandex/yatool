package ru.yandex.devtools.test;

import java.io.File;


public class Paths {
    static String sourceRoot;
    static String buildRoot;
    static String projectPath;
    static String sandboxResourcesRoot;
    static String testOutputsRoot;
    static String ytHddPath;
    static String workPath;

    public static String getSourcePath(String path) {
        return sourceRoot + File.separator + path;
    }

    public static String getSourcePath() {
        return sourceRoot;
    }

    public static String getBuildPath(String path) {
        return buildRoot + File.separator + path;
    }

    public static String getProjectPath(){
        return projectPath;
    }

    public static String getWorkPath() {
        return workPath;
    }

    public static String getSandboxResourcesRoot() {
        return sandboxResourcesRoot;
    }

    public static String getTestOutputsRoot() {
        return testOutputsRoot;
    }

    public static String getRamDrivePath() {
        return Params.params.get(Params.PARAM_RAM_DRIVE_PATH);
    }

    public static String getYtHddPath() {
        return ytHddPath;
    }
}
