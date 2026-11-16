from __future__ import annotations

import typing

import pytest
from pygls import uris

if typing.TYPE_CHECKING:
    pass
from .conftest import SlangClient


def assert_diagnostic_messages(diags, messages: list[str]):
    assert [diag.message for diag in diags.diagnostics] == messages


@pytest.mark.asyncio
async def test_publish_diagnostics(
    client: SlangClient,
    document_uri: str,
):
    """Ensure that diagnostics are published on open."""

    client.openText(
        document_uri,
        text="""
                    module m #() ()
                    endmodule
                    """,
    )

    diags = await client.wait_for_notification("textDocument/publishDiagnostics")

    assert len(diags.diagnostics) == 2
    assert diags.uri.isascii()
    assert " " not in diags.uri
    assert uris.to_fs_path(diags.uri) == uris.to_fs_path(document_uri)


@pytest.mark.asyncio
async def test_diags_update(
    client: SlangClient,
    document_uri: str,
):
    """Ensure that onChange sends out new diagnostics."""

    doc = client.openText(
        document_uri,
        text="""
                    module m #() ()
                    endmodule
                    """,
    )

    diags = await client.wait_for_notification("textDocument/publishDiagnostics")
    assert len(diags.diagnostics) == 2

    doc.append(
        text="""
            module m #() ()
            endmodule
                """
    )

    diags = await client.wait_for_notification("textDocument/publishDiagnostics")
    assert len(diags.diagnostics) == 4
