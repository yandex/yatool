This library loads all tool definitions from the build/tools directory and embeds them into the executable as resources.

If your test directly uses yalibrary.tools or tests code that accesses the tools library it is strongly recommended that
you only load the tools that are required for the test. It significantly reduces the number of tests triggered by the
build/tools directory modification.

Regardless of the tools selected, the toolchains from the build/tools/toolchains folder are always loaded in their entirety.

Recommended test ya.make snippet:
```
PY3TEST()

...

IF (AUTOCHECK)
    SET(AUTOCHECK_TOOL_LIST
        your-tool1
        your-tool2
    )
    INCLUDE(${ARCADIA_ROOT}/devtools/ya/conf/ya.make.inc)
ENDIF()

...

END()
```

To check the correctness of your ya.make locally, use the following command:
```
$(ya --print-path) make -rRttt -DAUTOCHECK path/to/your/test
```
