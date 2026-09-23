import enum


class UserClass(str, enum.Enum):
    DISTBUILD = 'distbuild'
    ROBOT = 'robot'
    ROOT_USER = 'root_user'
    SANDBOX = 'sandbox'
    USER = 'user'
    ZOMB = 'zomb'
    AGENT = 'agent'

    def __str__(self):
        return self.value
