import humanfriendly


class ProgressInfo:
    def __init__(self, downloaded=0, total=0):
        self._downloaded = downloaded
        self._total = total
        self._percent = None

    @property
    def percent(self):
        return self._percent

    @property
    def downloaded(self):
        return self._downloaded

    @property
    def total(self):
        return self._total

    @property
    def pretty_progress(self):
        """
        Examples:
        24% (56Kb)
        56Kb
        """
        downloaded = humanfriendly.format_size(self.downloaded)

        if self.percent is not None:
            return "{:.1f}% ({})".format(self.percent, downloaded)

        return '{}'.format(downloaded)

    def update_downloaded(self, downloaded):
        self._downloaded = downloaded
        self._calc_percent()

    def set_total(self, total):
        if total == 0:
            return
        self._total = total

    def _calc_percent(self):
        if self._downloaded == 0:
            self._percent = 0
        elif self._total is None or self._total == 0:
            self._percent = None
        else:
            self._percent = self.downloaded * 100 / self.total
