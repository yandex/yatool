import os


def setgroups_control(allow):
    filename = '/proc/self/setgroups'
    if os.path.exists(filename):
        with open(filename, 'w') as afile:
            afile.write("allow" if allow else "deny")


def map_id(path, from_id, to_id):
    with open(path, 'w') as afile:
        afile.write("{} {} 1".format(from_id, to_id))
