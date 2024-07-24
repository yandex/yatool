package ru.yandex.devtools.test;

import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class Params {
    static final String PARAM_RAM_DRIVE_PATH = "ram_drive_path";

    public static volatile Map<String, String> params;

    public static void setParams(List<String> paramsList) {
        Map<String, String> params = new HashMap<>();
        for (String param: paramsList) {
            if (param.contains("=")) {
                int pos = param.indexOf("=");
                String key = param.substring(0, pos);
                String value = param.substring(pos + 1);
                params.put(key, value);
            } else {
                params.put(param, "");
            }
        }

        String ramDrivePath = System.getenv("DISTBUILD_RAM_DISK_PATH");
        if (ramDrivePath != null) {
            params.put(PARAM_RAM_DRIVE_PATH, ramDrivePath);
        }

        Params.params = Collections.unmodifiableMap(params);
    }

}
