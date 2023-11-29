# coding: utf-8

import logging

logger = logging.getLogger(__name__)


class Reference(object):
    '''
    Incapsulates sandbox resource reference in ya.make files: resource identifier and optional target_dir download to
    '''
    def __init__(self, id, target_dir):
        self.__id = int(id)
        self.__target_dir = target_dir

    def __str__(self):
        return '{}={}'.format(self.__id, self.__target_dir) if self.__target_dir else str(self.__id)

    def get_id(self):
        return self.__id

    def get_target_dir(self):
        return self.__target_dir

    @staticmethod
    def from_uri(uri):
        """
        Parses sandbox uri
        :param uri: uri in form of sbr://<resource id>[=target_dir]
        :return: Reference object
        """
        scheme = "sbr://"
        if not uri.startswith(scheme):
            raise ValueError("Sandbox uri must be of 'sbr://<resource id>[=target_dir]' pattern, got '{}'".format(uri))
        return Reference.from_string(uri[len(scheme):])

    @staticmethod
    def from_string(val):
        try:
            resource_id, separator, target_dir = val.partition('=')
            if bool(separator) != bool(target_dir):
                raise ValueError("Invalid sandbox resource id '{}'".format(val))
            return Reference(int(resource_id.strip()), target_dir.strip())
        except ValueError:
            logger.error("Invalid sandbox resource: {}".format(val))
            raise

    @staticmethod
    def create(id, target_dir=''):
        return Reference(id, target_dir)


def get_id(resource):
    return resource.get_id() if isinstance(resource, Reference) else int(resource)


def create(obj):
    if isinstance(obj, Reference):
        return obj
    elif isinstance(obj, str):
        if obj.startswith("sbr://"):
            return Reference.from_uri(obj)
        else:
            return Reference.from_string(obj)
    else:
        return Reference.create(int(obj))
