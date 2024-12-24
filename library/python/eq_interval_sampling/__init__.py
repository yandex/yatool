# TODO py3 types


# from collections.abc import Sequence
# eq_interval_sampling[T](sequence: Sequence[T], nsamples: int) -> list[T]
def eq_interval_sampling(sequence, nsamples):
    if nsamples <= 0:
        raise AssertionError("nsamples must be > 0")
    if len(sequence) <= nsamples:
        return list(sequence)
    step = len(sequence) / nsamples
    indexes = (int(i * step) for i in range(nsamples))
    return [sequence[i] for i in indexes]
