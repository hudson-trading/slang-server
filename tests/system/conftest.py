import asyncio
import logging
import os
import pathlib
from concurrent.futures import Future
from dataclasses import dataclass
from typing import AsyncGenerator, Dict, List, Type, Union

import pytest
import pytest_asyncio
from lsprotocol import types
from pygls import uris
from pygls.exceptions import JsonRpcMethodNotFound
from pygls.lsp.client import BaseLanguageClient
from pygls.protocol import LanguageServerProtocol

logger = logging.getLogger(__name__)
os.environ["SLANG_SERVER_TESTS"] = "YES"


def pytest_addoption(parser):
    parser.addoption(
        "--rr",
        action="store_true",
        default=False,
        help="Run server with rr",
    )

    parser.addoption(
        "--gdb",
        action="store",
        default=None,
        help="Run server with gdb. Will wait for the given number of seconds to attach before continuing",
    )

    parser.addoption(
        "--binary",
        default="build/bin/slang-server",
        help="Slang server binary",
    )


@dataclass
class Flags:
    rr: bool = False
    gdb: Union[int, None] = None
    binary: str = "build/bin/slang-server"


@pytest.fixture
def my_options(request) -> Flags:
    """
    Returns a dictionary or any structure with the custom flags/params
    you might need in tests.
    """
    return Flags(
        rr=request.config.getoption("--rr"),
        gdb=request.config.getoption("--gdb"),
        binary=request.config.getoption("--binary"),
    )


class LanguageClientProtocol(LanguageServerProtocol):
    """An extended protocol class with extra methods that are useful for testing."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        self._notification_futures = {}

    def _handle_notification(self, method_name, params):
        if method_name == types.CANCEL_REQUEST:
            self._handle_cancel_notification(params.id)
            return

        future = self._notification_futures.pop(method_name, None)
        if future:
            future.set_result(params)

        try:
            handler = self._get_handler(method_name)
            self._execute_notification(handler, params)
        except (KeyError, JsonRpcMethodNotFound):
            logger.warning("Ignoring notification for unknown method '%s'", method_name)
        except Exception:
            logger.exception(
                "Failed to handle notification '%s': %s", method_name, params
            )

    def wait_for_notification(self, method: str, callback=None):
        future: Future = Future()
        if callback:

            def wrapper(future: Future):
                result = future.result()
                callback(result)

            future.add_done_callback(wrapper)

        self._notification_futures[method] = future
        return future

    def wait_for_notification_async(self, method: str):
        future = self.wait_for_notification(method)
        return asyncio.wrap_future(future)


stderr_log = logging.getLogger("[Server]")


async def stderr_readline(stop_event, reader):
    """Read lines from the given reader and pass them to lsp logger"""
    # Initialize message buffer
    while not stop_event.is_set():
        # Read a header line
        header = await reader.readline()
        if not header:
            break
        stderr_log.info(header.decode("utf-8").strip())


class SlangDoc:
    def __init__(self, cliet: "SlangClient", uri: str, text: str):
        self.client = cliet
        self.uri = uri
        self.text = text
        self.next_version = 1

    def positionFromOffset(self, offset: int) -> types.Position:
        line = 0
        character = 0
        for _line_no, line in enumerate(self.text.split("\n")):
            if offset < len(line):
                character = offset
                break
            offset -= len(line)
        return types.Position(line=_line_no, character=character)

    def onChange(self, changes: list[types.TextDocumentContentChangeEvent_Type1]):
        self.client.text_document_did_change(
            types.DidChangeTextDocumentParams(
                text_document=types.VersionedTextDocumentIdentifier(
                    uri=self.uri, version=self.next_version
                ),
                content_changes=changes,
            )
        )
        self.next_version += 1

    def append(self, text: str):
        # get pos from text
        pos = self.positionFromOffset(len(self.text))
        self.text += text
        self.onChange(
            [
                types.TextDocumentContentChangeEvent_Type1(
                    range=types.Range(start=pos, end=pos),
                    text=text,
                )
            ]
        )
        # partial updates


class SlangClient(BaseLanguageClient):
    """Language client used to drive test cases."""

    def __init__(
        self,
        protocol_cls: Type[LanguageClientProtocol] = LanguageClientProtocol,
        workspace_root: str = "",
        *args,
        **kwargs,
    ):
        super().__init__(
            "slang-server", "v1", *args, protocol_cls=protocol_cls, **kwargs
        )

        self.diagnostics: Dict[str, List[types.Diagnostic]] = {}
        """Used to hold any recieved diagnostics."""

        self.messages: List[types.ShowMessageParams] = []
        """Holds any received ``window/showMessage`` requests."""

        self.log_messages: List[types.LogMessageParams] = []
        """Holds any received ``window/logMessage`` requests."""

        self.workspace_root = workspace_root

    async def wait_for_notification(self, method: str):
        """Block until a notification with the given method is received.

        Parameters
        ----------
        method
           The notification method to wait for, e.g. ``textDocument/publishDiagnostics``
        """
        print(f"Waiting for notification {method}")
        return await self.protocol.wait_for_notification_async(method)

    def get_uri(self, rel_path: str) -> str:
        return uris.from_fs_path(pathlib.Path(self.workspace_root) / rel_path)

    def get_root_uri(self) -> str:
        return uris.from_fs_path(self.workspace_root)

    async def start(self, options: Flags):
        binary = options.binary
        args = []
        if options.rr:
            args = ["record", binary]
            binary = "rr"

        await super().start_io(binary, *args)

        if options.gdb:
            logger.warning(f"Run `gdb --pid {self._server.pid}`")
            logger.warning("Waiting for gdb to attach")
            await asyncio.sleep(int(options.gdb))

        self._stderr_log = asyncio.create_task(
            stderr_readline(self._stop_event, self._server.stderr)
        )
        # breakpoint()
        self._async_tasks.append(self._stderr_log)

    def openText(self, uri: str, text: str):
        doc = SlangDoc(self, uri, text)
        self.text_document_did_open(
            types.DidOpenTextDocumentParams(
                types.TextDocumentItem(
                    uri=uri,
                    language_id="systemverilog",
                    version=0,
                    text=text,
                )
            )
        )
        return doc

    async def getDocDocumentSymbols(self, uri: str) -> list[types.DocumentSymbol]:
        return await self.text_document_document_symbol_async(
            types.DocumentSymbolParams(
                types.TextDocumentIdentifier(uri),
            )
        )


def pytest_configure(config):
    logging.getLogger("pygls.client").setLevel(logging.DEBUG)
    logging.getLogger().setLevel(logging.DEBUG)


@pytest_asyncio.fixture(scope="function")
async def client(my_options: Flags) -> AsyncGenerator[SlangClient, None]:
    """
    Happy path client handling initialize and shutdown.
    """
    client = SlangClient()

    await client.start(my_options)

    response = await client.initialize_async(
        types.InitializeParams(
            capabilities=types.ClientCapabilities(),  # Ignored for now
            root_uri=client.get_root_uri(),
        )
    )
    assert response is not None

    yield client

    await client.shutdown_async(None)
    client.exit(None)

    await client.stop()
