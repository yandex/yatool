"""Describe suite metadata independently of payload storage and reporters."""

import typing


class SuiteInfo(typing.NamedTuple):
    uid: str | None
    project_path: str
    name: str
    target_platform_descriptor: str | None
    test_size: str
    ci_type: str
    skipped: bool
    result_uids: tuple[str, ...]
    output_uids: tuple[str, ...]

    def get_ci_type_name(self) -> str:
        return self.ci_type

    def is_skipped(self) -> bool:
        return self.skipped
