import pytest


def pytest_configure(config):
    config._inicache["python_functions"] = ("run",)
    config.option.tbstyle = "none"


@pytest.mark.tryfirst
def pytest_collection_modifyitems(items, config):
    for item in items:
        item._nodeid = item.nodeid.replace("exectest.py", "exectest")
