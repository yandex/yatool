GO_PROGRAM()

PEERDIR(${GOSTD}/fmt)

RUN_PYTHON3(gen_main.py STDOUT main.go)

END()
