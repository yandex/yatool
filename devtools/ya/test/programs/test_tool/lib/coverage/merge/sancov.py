import array
import os
import struct
import sys

import ujson as json


def merge_raw_sancov(files, dst):
    import sancov

    pcs = set()
    sys.stderr = open(os.devnull, "w")
    try:
        for filename in files:
            s = sancov.ReadOneFile(filename)
            pcs.update(s)
    finally:
        sys.stderr.close()
        sys.stderr = sys.__stderr__

    pcs = sorted(pcs)
    bits = 32
    if max(pcs) > 0xFFFFFFFF:
        bits = 64
    with open(dst, "wb") as afile:
        array.array('I', sancov.MagicForBits(bits)).tofile(afile)
        afile.write(struct.pack(sancov.TypeCodeForStruct(bits) * len(pcs), *pcs))


def merge_resolved_sancov(cov_files, test_mode):
    known_cov_fiedls = {"binary-hash", "covered-points", "point-symbol-info"}
    covered_points = set()
    point_symbol_info = {}
    binary_hash = {}

    for filename in cov_files:
        with open(filename) as afile:
            covdata = json.load(afile)

        assert set(covdata.keys()) == known_cov_fiedls, "Expected fields: {}. Got: {}".format(
            known_cov_fiedls, set(covdata.keys())
        )

        covered_points.update(covdata["covered-points"])

        for bname, bhash in covdata["binary-hash"].items():
            if bname in binary_hash:
                assert binary_hash[bhash] == bhash
            else:
                binary_hash[bname] = bhash

        for src_filename, symbol_info in covdata["point-symbol-info"].items():
            if src_filename not in point_symbol_info:
                point_symbol_info[src_filename] = symbol_info
                continue

            target_src_entry = point_symbol_info[src_filename]
            for symbol_name, addresses in symbol_info.items():
                if symbol_name not in target_src_entry:
                    target_src_entry[symbol_name] = addresses
                    continue

                if test_mode:
                    # pedant update
                    entry = target_src_entry[symbol_name]
                    for addr, pos in addresses.items():
                        if addr not in entry:
                            entry[addr] = pos
                        else:
                            assert entry[addr] == pos, (entry, addresses)
                else:
                    target_src_entry[symbol_name].update(addresses)

    return {
        "covered-points": list(covered_points),
        "point-symbol-info": point_symbol_info,
        "binary-hash": binary_hash,
    }
