LIBRARY()

IF (NEBIUS)
    YA_CONF_JSON(nebius/devtools/ya-bin/ya.conf.json)
ELSEIF (YA_OPENSOURCE)
    YA_CONF_JSON(devtools/ya/opensource/ya.conf.json)
ELSE()
    YA_CONF_JSON(build/ya.conf.json)
ENDIF()

END()
