import pytest

from bracelet_scripts.test_utils import Build


@pytest.fixture(scope="session")
def build() -> Build:
    return Build.get()
