LIBRARY()

IF (YA_OPENSOURCE)
    YA_TOOLS_CONF(devtools/ya/opensource)
ELSE()
    YA_TOOLS_CONF(build)
ENDIF()

END()
