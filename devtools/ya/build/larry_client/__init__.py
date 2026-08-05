class LarryClient:
    def __init__(self, callback):
        self.callback = callback

    def build(self, addr: str):
        raise NotImplementedError
