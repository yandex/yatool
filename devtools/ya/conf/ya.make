LIBRARY()

IF (YA_OPENSOURCE)
    YA_CONF_JSON(devtools/ya/opensource/ya.conf.json)
ELSE()
    YA_CONF_JSON(build/ya.conf.json)
ENDIF()

END()
