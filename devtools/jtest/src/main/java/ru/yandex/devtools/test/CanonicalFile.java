package ru.yandex.devtools.test;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

public class CanonicalFile {

    private String uri = null;
    private boolean local = false;
    private String[] diff_tool = null;
    private String diff_file_name = null;
    private int diff_tool_timeout = 0;

    public CanonicalFile(String path) {
        Path toBeCopied = Paths.get(path);
        if (!Files.exists(toBeCopied)) {
            throw new RuntimeException(String.format("Cannot find canonical file by %s", path));
        }
        try {
            Path tmp = Files.createTempDirectory("canonical");
            Path save_path = Files.copy(toBeCopied, Paths.get(tmp.toString(), toBeCopied.getFileName().toString()));
            uri = "file://" + save_path.toString();
        } catch (IOException e) {
            throw new RuntimeException(String.format("Error while preparing canonical file %s", e));
        }
    }

    public CanonicalFile(String path, boolean local) {
        this(path);
        this.local = local;
    }

    public CanonicalFile(String path, boolean local, String diffTool) {
        this(path, local);
        this.diff_tool = new String[]{diffTool};
    }

    public CanonicalFile(String path, boolean local, String diffTool, int diffToolTimeout) {
        this(path, local, diffTool);
        this.diff_tool_timeout = diffToolTimeout;
    }

    public CanonicalFile(String path, boolean local, String[] diffTool) {
        this(path, local);
        this.diff_tool = diffTool;
    }

    public CanonicalFile(String path, boolean local, String[] diffTool, int diffToolTimeout) {
        this(path, local, diffTool);
        this.diff_tool_timeout = diffToolTimeout;
    }
}
