import os
import ssl
import logging

from six.moves.urllib.request import urlopen, Request as UrlRequest
from six.moves.urllib.error import URLError

logger = logging.getLogger(__name__)


def upload_package(package_file_dir, _, opts):
    logger.info('Uploading package to specified url (nebius cloud way)')
    for rep in opts.publish_to_list:
        deb_file_name = list(filter(lambda x: x.endswith('.deb'), os.listdir(package_file_dir)))
        assert len(deb_file_name) == 1
        deb_file_name = os.path.join(package_file_dir, deb_file_name[0])
        assert os.path.exists(deb_file_name)
        with open(deb_file_name, 'rb') as f:
            deb_data = f.read()
        for _ in range(3):  # we have some problems with network now
            try:
                req = UrlRequest(rep)
                break
            except URLError as e:
                last_exception = e
        else:
            raise last_exception

        req.add_header("Authorization", "Bearer {}".format(opts.debian_upload_token))
        r = urlopen(req, data=deb_data, context=ssl._create_unverified_context())  # https://st.yandex-team.ru/NDT-294
        content = r.read()
        logger.info("Uploader responce code: {}".format(r.getcode()))
        logger.info("Uploader responce: {}".format(content))
        assert r.getcode() == 200, 'Upload http response code is {}'.format(r.getcode())
