RESET_CODES = {
    "FG": 39,
    "BG": 49,
    # "bold": 22,
    # "italic": 23,
    # "underline": 24,
    # "blink": 25,
    # "inverse": 27,
    # "hidden": 28,
    # "strikethrough": 29,
    # "framed": 54,
    # "overlined": 55,
}


def simplify_colors(data):
    """
    Some tools use light-* colors instead of simple ones, this yet to be supported by ya make
    (refer https://st.yandex-team.ru/FBP-999 for details)
    For now we can handle the light-* colors by transforming those into simple ones
    e.g. LIGHT_CYAN (96) -> CYAN (36)
    https://en.wikipedia.org/wiki/ANSI_escape_code#3-bit_and_4-bit
    """

    for code in range(30, 38):
        high_col = code + 60
        data = data.replace("\033[{}m".format(high_col), "\033[{}m".format(code))

    # ya make supports only full reset (\033[0m)
    # while node resets FG and BG targetly
    for code in RESET_CODES.values():
        data = data.replace("\033[{}m".format(code), "\033[0m")

    return data
