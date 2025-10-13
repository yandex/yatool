from devtools.ya.ide.gradle.config import _JavaSemConfig


class _WrapperPatcher:

    WRAPPER_PROPERTIES = 'gradle/wrapper/gradle-wrapper.properties'

    def __init__(self, java_sem_config: _JavaSemConfig):
        self.config: _JavaSemConfig = java_sem_config

    def patch_wrapper(self):
        path = self.config.export_root / self.WRAPPER_PROPERTIES
        patched_content = str()
        with path.open('r') as f:
            while line := f.readline():
                if line.startswith("distributionUrl"):
                    result = line.rsplit('/', 1)[0]
                    patched_content += result + "/gradle-8.14-bin.zip\n"
                else:
                    patched_content += line

        with path.open('w') as f:
            f.write(patched_content)
