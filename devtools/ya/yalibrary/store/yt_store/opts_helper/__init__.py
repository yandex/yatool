import humanfriendly


def parse_yt_max_cache_size(raw_size: str | None) -> str | int | None:
    if not raw_size:
        return None
    raw_size = raw_size.strip()
    if raw_size.endswith("%"):
        v = float(raw_size[:-1])
        if not (0 <= v <= 100):
            raise ValueError("out of range 0..100")
        return raw_size[:-1]
    try:
        return humanfriendly.parse_size(raw_size, binary=True)
    except humanfriendly.InvalidSize as e:
        raise ValueError(str(e))
