from devtools.ya.yalibrary.yandex.sandbox.misc import consts as sandbox_const


def get_resource_dict_sb(res_info, update_external=True):
    task_info = res_info['task']
    if 'status' in task_info:
        del task_info['status']
    ret = {
        "resource_id": res_info['id'],
        "resource_link": "{}/resource/{}/view".format(sandbox_const.DEFAULT_SANDBOX_URL, res_info['id']),
        "download_link": "{}/{}".format(sandbox_const.DEFAULT_SANDBOX_PROXY_URL, res_info['id']),
        "md5": res_info["md5"],
        "task": res_info["task"],
        "skynet_id": res_info['skynet_id'],
        "file_name": res_info['file_name']
    }
    if update_external:
        ret.update({
            "storage": "SANDBOX",
        })
    return ret
