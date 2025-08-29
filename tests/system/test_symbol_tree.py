from __future__ import annotations

import logging
import pathlib

import pytest
from lsprotocol import types

from .conftest import SlangClient

logger = logging.getLogger(__name__)


def tree_contains(
    tree: list[types.DocumentSymbol],
    name: str,
    kind: types.SymbolKind,
) -> bool:
    for symbol in tree:
        if symbol.name == name and symbol.kind == kind:
            return True

        if symbol.children:
            if tree_contains(symbol.children, name, kind):
                return True

    return False


@pytest.mark.asyncio
async def test_symbol_tree_regression(client: SlangClient) -> None:
    """
    Use a snapshot of `all.sv` to ensure that all expected symbols are correctly returned.
    """

    uri = "file:///mymodule.sv"

    all_sv = pathlib.Path.cwd().joinpath("tests/data/all.sv")

    with open(all_sv, "r") as fh:
        client.openText(uri=uri, text=fh.read())

    symbols = await client.getDocDocumentSymbols(uri)
    logger.info(symbols)

    # Count the total number of symbols in the hierarchy
    def count(nodes: list[types.DocumentSymbol]) -> int:
        counter = len(nodes)
        for node in nodes:
            if node.children:
                counter += count(node.children)

        return counter

    assert count(symbols) >= 147


@pytest.mark.asyncio
async def test_symbol_tree_defines(client: SlangClient) -> None:
    """
    Test that the symbol tree can correctly extract macro definitions.
    """

    uri = "file:///mymodule.sv"

    client.openText(
        uri,
        """
                    `define A
                    `ifdef A
                    `define B
                    `else
                    `define C
                    `endif

                    module MyModule();


                    `define SAFE_DEFINE(__name__, __value__=) \
                    `ifndef __name__ \
                        `define __name__ __value__ \
                    `endif

                    `SAFE_DEFINE(D)

                    `ifdef B
                    logic a;
                    `endif

                    endmodule
                    """,
    )

    symbols = await client.getDocDocumentSymbols(uri)
    logger.info(symbols)

    assert tree_contains(symbols, "A", types.SymbolKind.Constant)
    assert tree_contains(symbols, "B", types.SymbolKind.Constant)
    assert not tree_contains(symbols, "C", types.SymbolKind.Constant)
    assert tree_contains(symbols, "a", types.SymbolKind.Variable)

    assert tree_contains(symbols, "D", types.SymbolKind.Constant)
    assert tree_contains(symbols, "SAFE_DEFINE", types.SymbolKind.Constant)
