from libcpp cimport bool
from util.generic.string cimport TStringBuf, TString
from util.generic.vector cimport TVector
from util.generic.hash cimport THashMap

import logging
import re
import six

import exts.func

logger = logging.getLogger(__name__)


_PLATFORM_SEP = '-'


class PlatformNotSupportedException(Exception):
    mute = True


class InvalidPlatformSpecification(Exception):
    mute = True


cdef public object PyPlatformNotSupportedException = <object>PlatformNotSupportedException
cdef public object PyInvalidPlatformSpecification = <object>InvalidPlatformSpecification


cdef extern from *:
    """
        #include <Python.h>
        #include <devtools/libs/yaplatform/platform.h>

        extern "C++" PyObject* PyPlatformNotSupportedException;
        extern "C++" PyObject* PyInvalidPlatformSpecification;

        void _exception_handler() {
            try {
                throw;
            } catch (const NYa::TUnsupportedPlatformError& e) {
                PyErr_SetString(PyPlatformNotSupportedException, e.what());
            } catch (const NYa::TPlatformSpecificationError& e) {
                PyErr_SetString(PyInvalidPlatformSpecification, e.what());
            }
        }
    """
    cdef void _exception_handler()


cdef extern from "devtools/libs/yaplatform/platform.h" namespace "NYa":
    cppclass TCanonizedPlatform:
        TStringBuf ANY_PLATFORM
        TStringBuf DEFAULT_ARCH
        TStringBuf PLATFORM_SEP

        TCanonizedPlatform();
        TCanonizedPlatform(TStringBuf canonizedString);
        TCanonizedPlatform(TStringBuf os, TStringBuf arch);

        TString AsString() except +
        TString Os() except +
        TString Arch() except +

    cppclass TLegacyPlatform:
        TString Os
        TString Arch

    ctypedef THashMap[TCanonizedPlatform, TVector[TCanonizedPlatform]] TPlatformReplacements

    const TPlatformReplacements& DefaultPlatformReplacements()

    TString CurrentOs()
    TString CurrentArchitecture()
    TLegacyPlatform CurrentPlatform()
    bool IsWindows()
    bool IsDarwinArm64()
    TCanonizedPlatform MyPlatform()

    TCanonizedPlatform CanonizePlatform(TString platform) except + _exception_handler

    TString MatchPlatform(
        const TCanonizedPlatform& expect,
        const TVector[TString]& platforms,
        const TPlatformReplacements* platformReplacements) except + _exception_handler


cdef str _canonized_platform_to_str(const TCanonizedPlatform& cp):
    return six.ensure_str(cp.AsString())


cdef TCanonizedPlatform _canonized_platform_from_str(s: str):
    cdef TString b = six.ensure_binary(s)
    return TCanonizedPlatform(b)


def canonize_platform(platform_):
    return _canonized_platform_to_str(CanonizePlatform(six.ensure_binary(platform_)))


def canonize_full_platform(full_platform):
    full_platform = full_platform.lower()

    platform_ = re.match(r'^[a-z]+', full_platform).group(0)

    platform_suffix = ''
    for arch in ('aarch64', 'ppc64le', 'arm64'):
        if arch in full_platform:
            platform_suffix = _PLATFORM_SEP + arch
            break

    return canonize_platform(platform_ + platform_suffix)

@exts.func.lazy
def is_windows():
    return IsWindows()

@exts.func.lazy
def is_darwin_arm64():
    return IsDarwinArm64()


@exts.func.lazy
def is_darwin_rosetta():
    return my_platform() == 'darwin' and is_darwin_arm64()


@exts.func.lazy
def my_platform():
    return _canonized_platform_to_str(MyPlatform())


@exts.func.lazy
def _default_platform_replacements():
    replacements = {}
    cdef TPlatformReplacements def_pr = DefaultPlatformReplacements()
    for it in def_pr:
        replacements[_canonized_platform_to_str(it.first)] = [_canonized_platform_to_str(cp) for cp in it.second]
    return replacements


def get_platform_replacements(platform, custom_platform_replacements):
    platform_replacements = (
        _default_platform_replacements() if custom_platform_replacements is None else custom_platform_replacements
    )
    return platform_replacements.get(platform, [])


def match_platform(expect: str, platforms: list[str], custom_platform_replacements: dict[str, list[str]]=None):
    cdef TVector[TString] c_platforms
    for p in platforms:
        c_platforms.push_back(six.ensure_binary(p))

    cdef TCanonizedPlatform c_expect = _canonized_platform_from_str(expect)
    cdef TPlatformReplacements c_custom_platform_replacements
    cdef TPlatformReplacements* c_custom_platform_replacements_ptr = NULL
    if custom_platform_replacements is not None:
        for orig, replacements in custom_platform_replacements.items():
            for r in replacements:
                c_custom_platform_replacements[_canonized_platform_from_str(orig)].push_back(_canonized_platform_from_str(r))
        c_custom_platform_replacements_ptr = &c_custom_platform_replacements
    matched = MatchPlatform(c_expect, c_platforms, c_custom_platform_replacements_ptr)
    return None if matched.empty() else six.ensure_str(matched)


@exts.func.lazy
def current_architecture():
    return six.ensure_str(CurrentArchitecture())


@exts.func.lazy
def current_os():
    return six.ensure_str(CurrentOs())


def current_platform():
    return {'os': current_os(), 'arch': current_architecture()}


@exts.func.lazy
def current_toolchain():
    res = current_platform().copy()

    res['toolchain'] = 'default'

    return res


def stringize_platform(p, sep=_PLATFORM_SEP):
    return (p['toolchain'] + sep + p['os'] + sep + p['arch']).upper()


def prevalidate_platform(s):
    return s.count(_PLATFORM_SEP) == 2


def parse_platform(s):
    if not prevalidate_platform(s):
        raise InvalidPlatformSpecification('Unsupported platform: %s' % s)

    toolchain, os, arch = s.split(_PLATFORM_SEP)

    return {'toolchain': toolchain.lower(), 'os': os.upper(), 'arch': arch.lower()}


# XXX: mine from ya.conf.json
def guess_os(s):
    s = s.upper()

    if s.startswith('WIN'):
        s = 'WIN'

    if s in ('LINUX', 'WIN', 'FREEBSD', 'DARWIN'):
        return s

    return None


def guess_platform(s):
    s = s.upper()

    if not s.count(_PLATFORM_SEP):
        cp = current_platform()
        os = guess_os(s)

        if os:
            return 'DEFAULT-{}-{}'.format(os, cp['arch'])

        return '{}-{}-{}'.format(s, cp['os'], cp['arch'])

    return s
