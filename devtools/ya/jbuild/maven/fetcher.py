import xml.etree.ElementTree as et
import six.moves.urllib as urllib
import logging

from exts import http_client
from exts import retry

logger = logging.getLogger(__name__)


class FetchException(Exception):
    pass


def is_snapshot(ver):
    return 'SNAPSHOT' in ver


def fix_mvn_xml(xml):
    def remove_namespace(doc, namespace):
        ns = u'{%s}' % namespace
        nsl = len(ns)
        for elem in doc.iter():
            if elem.tag.startswith(ns):
                elem.tag = elem.tag[nsl:]

    remove_namespace(xml, 'http://maven.apache.org/POM/4.0.0')

    return xml


def artifact_dirname(repo, g, a, v):
    return '/'.join([repo.rstrip('/'), g.replace('.', '/'), a, v])


@retry.retrying(max_times=2)
def fetch_small(url):
    return urllib.request.urlopen(url).read()


def iter_snapshot_versions(repo, g, a, v):
    meta_url = artifact_dirname(repo, g, a, v) + '/maven-metadata.xml'

    try:
        meta = fetch_small(meta_url)

    except Exception as e:
        logger.debug(e)
        return

    try:
        meta = fix_mvn_xml(et.fromstring(meta))

    except Exception as e:
        logger.debug(e)
        return

    try:
        snapshot = meta.find('./versioning/snapshot')

        t = snapshot.find('timestamp').text

        try:
            bn = snapshot.find('buildNumber').text

        except Exception:
            bn = '0'

        yield v.replace('SNAPSHOT', t + '-' + bn)

    except Exception as e:
        logger.debug(e)
        pass

    try:
        vers_xml = meta.findall('./versioning/snapshotVersions/snapshotVersion')

    except Exception as e:
        logger.debug(e)
        return

    vers = []

    for x in vers_xml:
        try:
            ver = x.find('value').text

        except Exception as e:
            logger.debug(e)
            continue

        try:
            upd = x.find('updated').text

        except Exception as e:
            logger.debug(e)
            upd = None

        vers.append((ver, upd))

    vers.sort(key=lambda x: x[1], reverse=True)
    for ver, upd in vers:
        yield ver


def iter_versions(repo, g, a, v, sv=None):
    if sv:
        yield sv

    else:
        if is_snapshot(v):
            for x in iter_snapshot_versions(repo, g, a, v):
                yield x

        yield v


def fetch_pom(repos, g, a, v, snapshot_ver=None):
    errs = []

    for r in repos:
        url_d = artifact_dirname(r, g, a, v)

        for sv in iter_versions(r, g, a, v, snapshot_ver):
            pom_url = url_d + '/' + a + '-' + sv + '.pom'

            try:
                pom = fetch_small(pom_url)

            except Exception as e:
                errs.append((pom_url, e))

                continue

            try:
                pom = fix_mvn_xml(et.fromstring(pom))

            except Exception as e:
                errs.append((pom_url, e))

            logger.info('Fetched %s', pom_url)
            return r, sv, pom

    raise FetchException(
        'Couldn\'t fetch pom for {}-{}-{}:\n{}'.format(g, a, v, '\n'.join([' '.join(map(str, x)) for x in errs]))
    )


def fetch_jar(repos, g, a, v, dest, suffix=None, snapshot_ver=None):
    errs = []

    for r in repos:
        url_d = artifact_dirname(r, g, a, v)

        for sv in iter_versions(r, g, a, v, snapshot_ver):
            jar_url = url_d + '/' + a + '-' + sv + (('-' + suffix) if suffix else '') + '.jar'
            jar_md5_url = jar_url + '.md5'

            try:
                jar_md5 = fetch_small(jar_md5_url)

            except Exception:
                jar_md5 = None

            try:
                http_client.download_file(jar_url, dest, mode=0o777, expected_md5=jar_md5)

            except Exception as e:
                errs.append((jar_url, e))

                continue

            return r, sv, dest

    raise FetchException(
        'Couldn\'t fetch jar for {}-{}-{}:\n{}'.format(g, a, v, '\n'.join([' '.join(map(str, x)) for x in errs]))
    )
