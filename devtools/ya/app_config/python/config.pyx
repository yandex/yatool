from libcpp cimport bool
from util.generic.string cimport TString

cdef extern from "devtools/ya/app_config/lib/config.h" namespace "NYa::NConfig" nogil:
    cdef TString Description
    cdef bool HasMapping
    cdef bool InHouse
    cdef bool HaveSandboxFetcher
    cdef bool HaveOAuthSupport
    cdef TString JunkRoot
    cdef TString ExtraConfRoot


description = Description.decode()
has_mapping = HasMapping
in_house = InHouse
have_sandbox_fetcher = HaveSandboxFetcher
have_oauth_support = HaveOAuthSupport
junk_root = JunkRoot.decode()
extra_conf_root = ExtraConfRoot.decode()
