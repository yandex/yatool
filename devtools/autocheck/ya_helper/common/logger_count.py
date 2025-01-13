import logging


class LoggerCounter:
    """Inject logger into class instance and count class instances"""

    __count = 0

    logger = None  # type: Logger

    def __new__(cls, *args, **kwargs):
        logger = logging.getLogger(cls.__module__)

        if not hasattr(cls, 'logger') or cls.logger is None:
            cls.logger = logger.getChild("{}".format(cls.__name__))

        obj = super().__new__(cls)
        obj.logger = cls.logger.getChild("#{}".format(cls.__count))
        obj._instance_number = cls.__count
        cls.__count += 1
        return obj

    @property
    def _class_name(self):
        return "{}#{}".format(self.__class__.__name__, self._instance_number)
