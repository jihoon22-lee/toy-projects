"""Default-safe environment and path redaction tests."""

from __future__ import annotations

from envlens.redaction import (
    REDACTED,
    USER_HOME,
    is_sensitive_name,
    redact_environment,
    redact_text,
    redact_value,
)


def test_sensitive_environment_names_cover_common_secret_variants() -> None:
    sensitive = (
        "AWS_ACCESS_KEY_ID",
        "service-api-key",
        "authorization",
        "SESSION_COOKIE",
        "DB_CREDENTIALS",
        "db-password",
        "SSH_PRIVATE_KEY",
        "client-secret",
        "refresh-token",
    )
    for name in sensitive:
        assert is_sensitive_name(name), name
    assert not is_sensitive_name("LANG")
    assert not is_sensitive_name("BUILD_NUMBER")


def test_redact_environment_redacts_values_and_sorts_names() -> None:
    environment = {
        "ZED": "/home/alice/project",
        "API_KEY": "plain secret",
        "HOME": "/home/alice",
        "LANG": "en_US.UTF-8",
        "db-password": "another secret",
    }

    redacted = redact_environment(environment, ("/home/alice",))

    assert list(redacted) == sorted(environment)
    assert redacted["API_KEY"] == REDACTED
    assert redacted["db-password"] == REDACTED
    assert redacted["HOME"] == USER_HOME
    assert redacted["ZED"] == USER_HOME + "/project"
    assert redacted["LANG"] == "en_US.UTF-8"


def test_redact_text_handles_posix_home_case_insensitively() -> None:
    value = "/home/alice/project /HOME/ALICE/cache /var/tmp"

    redacted = redact_text(value, ("/home/alice",))

    assert redacted == USER_HOME + "/project " + USER_HOME + "/cache /var/tmp"


def test_redact_text_handles_windows_home_slash_variants() -> None:
    home = r"C:\Users\Alice"
    value = r"C:\Users\Alice\AppData C:/Users/Alice/AppData c:/users/alice"

    redacted = redact_text(value, (home,))

    assert redacted == USER_HOME + r"\AppData " + USER_HOME + "/AppData " + USER_HOME


def test_redact_text_scrubs_url_userinfo_and_secret_query_values() -> None:
    value = (
        "pkg @ https://alice:hunter2@example.invalid/simple?token=visible&safe=yes "
        "https://example.invalid/path?api_key=also-visible#fragment"
    )

    redacted = redact_text(value, ())

    assert "alice" not in redacted
    assert "hunter2" not in redacted
    assert "visible" not in redacted
    assert "also-visible" not in redacted
    assert redacted.count(REDACTED) == 3
    assert "safe=yes" in redacted


def test_url_bearing_environment_names_are_sensitive() -> None:
    for name in ("PIP_INDEX_URL", "uv-index-url", "PACKAGE_REPOSITORY", "registry"):
        assert is_sensitive_name(name), name


def test_redact_value_recurses_and_produces_deterministic_mapping_order() -> None:
    value = {
        "z": ["/srv/alice/a", {"nested": "/srv/alice/b"}],
        "a": "untouched",
    }

    redacted = redact_value(value, ("/srv/alice",))

    assert list(redacted) == ["a", "z"]
    assert redacted == {
        "a": "untouched",
        "z": [USER_HOME + "/a", {"nested": USER_HOME + "/b"}],
    }
