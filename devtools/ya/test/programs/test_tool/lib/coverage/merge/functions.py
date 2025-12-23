def merge_functions_inplace(result, record):
    for start_pos, entries in record.items():
        if start_pos not in result:
            result[start_pos] = entries
            continue

        target = result[start_pos]
        for funcname, count in entries.items():
            if funcname not in target:
                target[funcname] = count
            else:
                target[funcname] += count
