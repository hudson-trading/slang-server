from __future__ import annotations

import typing

import pytest
from lsprotocol import types

if typing.TYPE_CHECKING:
    pass
from .conftest import SlangClient


@pytest.mark.asyncio
async def test_publish_diagnostics(
    client: SlangClient,
):
    """Ensure that diagnostics are published on open."""

    uri = "file:///mymodule.sv"
    client.openText(
        uri,
        text="""
                    module m #() ()
                    endmodule
                    """,
    )

    diags = await client.wait_for_notification("textDocument/publishDiagnostics")

    assert len(diags.diagnostics) == 1
    assert diags.uri == uri


@pytest.mark.asyncio
async def test_diags_update(
    client: SlangClient,
):
    """Ensure that onChange sends out new diagnostics."""

    test_uri = "file:///mymodule.sv"

    doc = client.openText(
        test_uri,
        text="""
                    module m #() ()
                    endmodule
                    """,
    )

    diags = await client.wait_for_notification("textDocument/publishDiagnostics")
    assert len(diags.diagnostics) == 1

    doc.append(
        text="""
            module m #() ()
            endmodule
                """
    )

    diags = await client.wait_for_notification("textDocument/publishDiagnostics")
    assert len(diags.diagnostics) == 6
