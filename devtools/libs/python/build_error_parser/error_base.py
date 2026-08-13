class Error:
    ERROR_RE = None
    MESSAGE = ''

    @classmethod
    def is_error_found(cls, message: str) -> bool:
        if cls.ERROR_RE:
            return cls._search(cls.ERROR_RE, message)
        return False

    @classmethod
    def _search(cls, regexp, text: str) -> bool:
        return bool(regexp.search(text))
