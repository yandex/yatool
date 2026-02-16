package ru.yandex.devtools.test;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.reflect.Field;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Base64;
import java.util.List;
import java.util.Properties;
import java.util.Scanner;

import com.beust.jcommander.Parameter;
import com.beust.jcommander.converters.IParameterSplitter;
import com.google.gson.Gson;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import ru.yandex.devtools.eval.EvalRecurrentProperties;
import ru.yandex.devtools.eval.RecurrentProperties;

public class Shared {

    public static final Gson GSON = new Gson();

    private Shared() {
        //
    }

    // Use custom splitter to avoid splitting test filter names with comma
    public static class NonSplitter implements IParameterSplitter {
        public List<String> split(String value) {
            List<String> list = new ArrayList<>();
            list.add(value);
            return list;
        }
    }

    public static class TagSplitter implements IParameterSplitter {
        public List<String> split(String value) {
            return Arrays.asList(value.split("\\+|\\s"));
        }
    }

    //CHECKSTYLE:OFF
    public static class Parameters {
        @Parameter(names = {"-o", "--output"})
        public String output;

        @Parameter(names = {"-S", "--source-root"})
        public String sourceRoot;

        @Parameter(names = {"-B", "--build-root"})
        public String buildRoot;

        @Parameter(names = {"-a", "--sandbox-resources-root"})
        public String sandboxResourcesRoot;

        @Parameter(names = {"-e", "--test-outputs-root"})
        public String testOutputsRoot;

        @Parameter(names = {"-m", "--modulo"})
        public int modulo;

        @Parameter(names = {"-i", "--modulo-index"})
        public int moduloIndex;

        @Parameter(names = {"-L", "--list"})
        public boolean list;

        @Parameter(names = {"-s", "--fork-subtests"})
        public boolean forkSubtests;

        @Parameter(names = {"-p", "--test-partition"})
        public String testPartition;

        @Parameter(names = {"--experimental-fork"})
        public boolean experimentalFork;

        @Parameter(names = {"-F", "--filter"}, splitter = NonSplitter.class)
        public List<String> filters = new ArrayList<>();

        @Parameter(names = {"--junit-tags"}, splitter = TagSplitter.class)
        public List<String> junit_tags = new ArrayList<>();

        @Parameter(names = {"-t", "--tests-jar"}, required = true)
        public String testsJar;

        @Parameter(names = {"-l", "--runner-log-path"})
        public String runnerLogPath;

        @Parameter(names = {"--allure"})
        public boolean allure = false;

        @Parameter(names = {"--properties"})
        public String rawProperties = null;

        @Parameter(names = {"--test-param"})
        public List<String> testParams = new ArrayList<>();

        @Parameter(names = {"--test-list-path"})
        public String testListPath = "";
    }
    //CHECKSTYLE:ON

    public static Properties loadProperties(String rawProperties) {
        Properties properties = new Properties();
        properties.putAll(System.getProperties());

        String jsonString = new String(Base64.getDecoder().decode(rawProperties), StandardCharsets.UTF_8);
        // Must be compatible with JsonParser 2.8.2
        JsonArray jsonArray = new JsonParser().parse(jsonString).getAsJsonArray();

        for (JsonElement el : jsonArray) {
            JsonObject obj = el.getAsJsonObject();
            String type = obj.get("type").getAsString();

            if (type.equals("inline")) {
                String key = obj.get("key").getAsString();

                String value = "";
                if (!obj.get("value").isJsonNull()) {
                    value = obj.get("value").getAsString();
                }

                properties.setProperty(key, value);

            } else if (type.equals("file")) {
                String path = EvalRecurrentProperties.eval(obj.get("path").getAsString(), System.getProperties());
                File propsFile = new File(path);

                Properties fileProps = new Properties();
                try {
                    if (path.endsWith(".xml")) {
                        fileProps.loadFromXML(new FileInputStream(propsFile));

                    } else {
                        fileProps.load(new FileInputStream(propsFile));
                    }

                } catch (IOException e) {
                    throw new RuntimeException(e);
                }

                properties.putAll(fileProps);

            } else {
                throw new RuntimeException("Unknown property type: " + type);
            }
        }

        return new RecurrentProperties(properties);
    }

    public static void extendSystemProperties(Properties properties) {
        for (Object obj : properties.keySet()) {
            System.setProperty((String) obj, properties.getProperty((String) obj));
        }
    }

    public static Object getDeclaredFieldValue(Object obj, String field) throws Exception {
        Field f = obj.getClass().getDeclaredField(field);
        f.setAccessible(true);
        return f.get(obj);
    }

    public static String ensureUTF8(String string) {
        return string == null ? null : new String(string.getBytes(StandardCharsets.UTF_8), StandardCharsets.UTF_8);
    }

    public static String safeFileName(String name) {
        name = name.replace('/', '_');
        name = name.replace(' ', '.');
        name = name.replace((char) 0, '.');
        return name;
    }

    public static <T> MethodHandle getFieldAccessor(Class<?> clazz, String fieldName) {
        try {
            Field f = clazz.getDeclaredField(fieldName);
            f.setAccessible(true);
            return MethodHandles.lookup().unreflectGetter(f);
        } catch (Exception e) {
            throw new RuntimeException("Unable to access field " + fieldName + " in class " + clazz, e);
        }
    }

    public static boolean loadFilters(Parameters params) throws FileNotFoundException {
        if (params.testListPath.isEmpty()) {
            return false;
        }
        File jsonTestListFile = new File(params.testListPath);
        if (jsonTestListFile.exists()) {
            Scanner myReader = new Scanner(jsonTestListFile);
            String jsonString = "";
            while (myReader.hasNextLine()) {
                jsonString = myReader.nextLine();
            }
            String[] data = GSON.fromJson(jsonString, String[][].class)[params.moduloIndex];
            params.filters.clear();
            params.filters = new ArrayList<>(Arrays.asList(data));
            return true;
        } else {
            return false;
        }
    }

    public static void initTmpDir() {
        File tmpDir = new File(System.getProperty("java.io.tmpdir"));

        if (!tmpDir.exists()) {
            //noinspection ResultOfMethodCallIgnored
            tmpDir.mkdir();
        }
    }

}
