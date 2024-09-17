import json
import os
import sys
import exts.fs
import exts.archive

import devtools.ya.test.system.process as process


def prepare(simctl_bin, profiles_path, test_cwd, device_name, ios_app_tar, device_type, runtime):
    temp_dir = os.path.join(test_cwd, 'ios_test_app')
    exts.fs.ensure_dir(temp_dir)
    exts.archive.extract_from_tar(ios_app_tar, temp_dir)
    expected_app_name = os.path.join(temp_dir, os.path.basename(ios_app_tar)[: -len('.ios.tar')] + '.app')
    if not os.path.isdir(expected_app_name):
        raise Exception(expected_app_name + " is not iOS simulator application")
    devices_dir = get_devices_dir(test_cwd)
    exts.fs.ensure_dir(devices_dir)
    all_devices = list(get_all_devices(simctl_bin, profiles_path, devices_dir))
    if [x for x in all_devices if x["name"] == device_name]:
        raise Exception("Device named {} already exists".format(device_name))
    process.execute(
        [simctl_bin, "--profiles", profiles_path, "--set", devices_dir, "create", device_name, device_type, runtime],
        check_exit_code=True,
    )
    created = get_device_by_name(device_name, simctl_bin, profiles_path, devices_dir)
    if not created["isAvailable"]:
        raise Exception(
            "Creation error: temp device {} status is {} (True expected)".format(device_name, created["isAvailable"])
        )
    process.execute(
        [simctl_bin, "--set", devices_dir, "boot", device_name],
        check_exit_code=True,
    )
    booted = get_device_by_name(device_name, simctl_bin, profiles_path, devices_dir)
    if booted["state"] != "Booted":
        raise Exception("Boot error: temp device {} state is {} (Booted expected)".format(device_name, booted["state"]))
    process.execute([simctl_bin, "--set", devices_dir, "install", device_name, expected_app_name], check_exit_code=True)


def run(simctl_bin, profiles_path, test_cwd, device_name, cmd_args):
    devices_dir = get_devices_dir(test_cwd)
    res = process.execute(
        [simctl_bin, "--set", devices_dir, 'launch', '--console-pty', device_name, 'Yandex.devtools_ios_wraper']
        + cmd_args,
        #       Can't use stdout=subprocess.PIPE here! It leads to deadlocks! See MAPSMOBCORE-12317
    )

    sys.stdout.write(res.stdout)
    return res


def cleanup(simctl_bin, profiles_path, test_cwd, device_name):
    devices_dir = get_devices_dir(test_cwd)
    process.execute([simctl_bin, "--set", devices_dir, "uninstall", device_name, 'Yandex.devtools_ios_wraper'])
    process.execute([simctl_bin, "--set", devices_dir, "shutdown", device_name])


def get_devices_dir(test_cwd):
    return os.path.join(test_cwd, 'ios_test_app', 'devices')


def get_device_by_name(device_name, simctl_bin, profiles_path, devices_dir):
    devices = [x for x in get_all_devices(simctl_bin, profiles_path, devices_dir) if x["name"] == device_name]
    if not devices:
        raise Exception("Error: device named {} not found".format(device_name))
    return devices[0]


def get_all_devices(simctl, profiles, device_dir):
    res = process.execute([simctl, "--set", device_dir, "list", "--json", "devices"])
    sys.stdout.write(res.stdout)
    rc = res.exit_code
    if rc:
        raise Exception("Devices list command return code is {}\nstdout:\n{}".format(rc, res.stdout))
    raw_object = json.loads(res.stdout)
    if "devices" not in raw_object:
        raise Exception("Devices not found in\n{}".format(json.dumps(raw_object)))
    raw_object = raw_object["devices"]
    for os_name, devices in raw_object.items():
        for device in devices:
            device["os_name"] = os_name
            yield device
