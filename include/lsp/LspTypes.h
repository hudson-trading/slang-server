//------------------------------------------------------------------------------
// LspTypes.h
// LSP protocol type definitions and data structures
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#pragma once
#include "JsonTypes.h"
#include "URI.h"
#include <cstdint>
#include <optional>
#include <rfl/json.hpp>
#include <variant>
#include <vector>

namespace lsp {

/// A workspace folder inside a client.
struct WorkspaceFolder {
    /// The associated URI for this workspace folder.
    URI uri;

    /// The name of the workspace folder. Used to refer to this
    /// workspace folder in the user interface.
    std::string name;
};

/// The glob pattern to watch relative to the base path. Glob patterns can have the following
/// syntax:
/// - `*` to match one or more characters in a path segment
/// - `?` to match on one character in a path segment
/// - `**` to match any number of path segments, including none
/// - `{}` to group conditions (e.g. `**​/*.{ts,js}` matches all TypeScript and JavaScript files)
/// - `[]` to declare a range of characters to match in a path segment (e.g., `example.[0-9]` to
/// match on `example.0`, `example.1`, …)
/// - `[!...]` to negate a range of characters to match in a path segment (e.g., `example.[!0-9]` to
/// match on `example.a`, `example.b`, but not `example.0`)
///
/// @since 3.17.0
using Pattern = std::string;

/// A relative pattern is a helper to construct glob patterns that are matched
/// relatively to a base URI. The common value for a `baseUri` is a workspace
/// folder root, but it can be another absolute URI as well.
///
/// @since 3.17.0
struct RelativePattern {
    /// A workspace folder or a base URI to which this pattern will be matched
    /// against relatively.
    rfl::Variant<WorkspaceFolder, URI> baseUri;

    /// The actual glob pattern;
    Pattern pattern;
};

/// The glob pattern. Either a string pattern or a relative pattern.
///
/// @since 3.17.0
using GlobPattern = rfl::Variant<Pattern, RelativePattern>;

/// Position in a text document expressed as zero-based line and character
/// offset. Prior to 3.17 the offsets were always based on a UTF-16 string
/// representation. So a string of the form `a𐐀b` the character offset of the
/// character `a` is 0, the character offset of `𐐀` is 1 and the character
/// offset of b is 3 since `𐐀` is represented using two code units in UTF-16.
/// Since 3.17 clients and servers can agree on a different string encoding
/// representation (e.g. UTF-8). The client announces it's supported encoding
/// via the client capability
/// [`general.positionEncodings`](https://microsoft.github.io/language-server-protocol/specifications/specification-current/#clientCapabilities).
/// The value is an array of position encodings the client supports, with
/// decreasing preference (e.g. the encoding at index `0` is the most preferred
/// one). To stay backwards compatible the only mandatory encoding is UTF-16
/// represented via the string `utf-16`. The server can pick one of the
/// encodings offered by the client and signals that encoding back to the
/// client via the initialize result's property
/// [`capabilities.positionEncoding`](https://microsoft.github.io/language-server-protocol/specifications/specification-current/#serverCapabilities).
/// If the string value `utf-16` is missing from the client's capability `general.positionEncodings`
/// servers can safely assume that the client supports UTF-16. If the server
/// omits the position encoding in its initialize result the encoding defaults
/// to the string value `utf-16`. Implementation considerations: since the
/// conversion from one encoding into another requires the content of the
/// file / line the conversion is best done where the file is read which is
/// usually on the server side.
///
/// Positions are line end character agnostic. So you can not specify a position
/// that denotes `\r|\n` or `\n|` where `|` represents the character offset.
///
/// @since 3.17.0 - support for negotiated position encoding.

using uint = uint32_t;
struct Position {
    /// Line position in a document (zero-based).
    uint line;

    /// Character offset on a line in a document (zero-based).
    ///
    /// The meaning of this offset is determined by the negotiated
    /// `PositionEncodingKind`.
    uint character;

    auto operator<=>(const Position&) const = default;
};

/// A notebook document filter where `scheme` is required field.
///
/// @since 3.18.0
struct NotebookDocumentFilterScheme {
    /// The type of the enclosing notebook.
    std::optional<std::string> notebookType;

    /// A Uri {@link Uri.scheme scheme}, like `file` or `untitled`.
    std::string scheme;

    /// A glob pattern.
    std::optional<GlobPattern> pattern;
};

/// A notebook document filter where `pattern` is required field.
///
/// @since 3.18.0
struct NotebookDocumentFilterPattern {
    /// The type of the enclosing notebook.
    std::optional<std::string> notebookType;

    /// A Uri {@link Uri.scheme scheme}, like `file` or `untitled`.
    std::optional<std::string> scheme;

    /// A glob pattern.
    GlobPattern pattern;
};

/// A notebook document filter where `notebookType` is required field.
///
/// @since 3.18.0
struct NotebookDocumentFilterNotebookType {
    /// The type of the enclosing notebook.
    std::string notebookType;

    /// A Uri {@link Uri.scheme scheme}, like `file` or `untitled`.
    std::optional<std::string> scheme;

    /// A glob pattern.
    std::optional<GlobPattern> pattern;
};

/// A document filter where `scheme` is required field.
///
/// @since 3.18.0
struct TextDocumentFilterScheme {
    /// A language id, like `typescript`.
    std::optional<std::string> language;

    /// A Uri {@link Uri.scheme scheme}, like `file` or `untitled`.
    std::string scheme;

    /// A glob pattern, like **​/*.{ts,js}. See TextDocumentFilter for examples.
    ///
    /// @since 3.18.0 - support for relative patterns. Whether clients support
    /// relative patterns depends on the client capability
    /// `textDocuments.filters.relativePatternSupport`.
    std::optional<GlobPattern> pattern;
};

/// A document filter where `pattern` is required field.
///
/// @since 3.18.0
struct TextDocumentFilterPattern {
    /// A language id, like `typescript`.
    std::optional<std::string> language;

    /// A Uri {@link Uri.scheme scheme}, like `file` or `untitled`.
    std::optional<std::string> scheme;

    /// A glob pattern, like **​/*.{ts,js}. See TextDocumentFilter for examples.
    ///
    /// @since 3.18.0 - support for relative patterns. Whether clients support
    /// relative patterns depends on the client capability
    /// `textDocuments.filters.relativePatternSupport`.
    GlobPattern pattern;
};

/// A document filter where `language` is required field.
///
/// @since 3.18.0
struct TextDocumentFilterLanguage {
    /// A language id, like `typescript`.
    std::string language;

    /// A Uri {@link Uri.scheme scheme}, like `file` or `untitled`.
    std::optional<std::string> scheme;

    /// A glob pattern, like **​/*.{ts,js}. See TextDocumentFilter for examples.
    ///
    /// @since 3.18.0 - support for relative patterns. Whether clients support
    /// relative patterns depends on the client capability
    /// `textDocuments.filters.relativePatternSupport`.
    std::optional<GlobPattern> pattern;
};

/// A range in a text document expressed as (zero-based) start and end positions.
///
/// If you want to specify a range that contains a line including the line ending
/// character(s) then use an end position denoting the start of the next line.
/// For example:
/// ```ts
/// {
///     start: { line: 5, character: 23 }
///     end : { line 6, character : 0 }
/// }
/// ```
struct Range {
    /// The range's start position.
    Position start;

    /// The range's end position.
    Position end;

    auto operator<=>(const Range&) const = default;
};

/// A notebook document filter denotes a notebook document by
/// different properties. The properties will be match
/// against the notebook's URI (same as with documents)
///
/// @since 3.17.0
using NotebookDocumentFilter =
    rfl::Variant<NotebookDocumentFilterNotebookType, NotebookDocumentFilterScheme,
                 NotebookDocumentFilterPattern>;

/// How whitespace and indentation is handled during completion
/// item insertion.
///
/// @since 3.16.0
enum class InsertTextMode : uint8_t {
    asIs = 1,
    adjustIndentation = 2,
};

/// Matching options for the file operation pattern.
///
/// @since 3.16.0
struct FileOperationPatternOptions {
    /// The pattern should be matched ignoring casing.
    std::optional<bool> ignoreCase;
};

/// A pattern kind describing if a glob pattern matches a file a folder or
/// both.
///
/// @since 3.16.0
using FileOperationPatternKind = rfl::Literal<"file", "folder">;

/// The diagnostic tags.
///
/// @since 3.15.0
enum class DiagnosticTag : uint8_t {
    Unnecessary = 1,
    Deprecated = 2,
};

/// Completion item tags are extra annotations that tweak the rendering of a completion
/// item.
///
/// @since 3.15.0
enum class CompletionItemTag : uint8_t {
    Deprecated = 1,
};

/// A set of predefined code action kinds
using CodeActionKind = rfl::Literal<"", "quickfix", "refactor", "refactor.extract",
                                    "refactor.inline", "refactor.rewrite", "source",
                                    "source.organizeImports", "source.fixAll", "notebook">;

/// A document filter denotes a document by different properties like
/// the {@link TextDocument.languageId language}, the {@link Uri.scheme scheme} of
/// its resource, or a glob-pattern that is applied to the {@link TextDocument.fileName path}.
///
/// Glob patterns can have the following syntax:
/// - `*` to match one or more characters in a path segment
/// - `?` to match on one character in a path segment
/// - `**` to match any number of path segments, including none
/// - `{}` to group sub patterns into an OR expression. (e.g. `**​/*.{ts,js}` matches all
/// TypeScript and JavaScript files)
/// - `[]` to declare a range of characters to match in a path segment (e.g., `example.[0-9]` to
/// match on `example.0`, `example.1`, …)
/// - `[!...]` to negate a range of characters to match in a path segment (e.g., `example.[!0-9]` to
/// match on `example.a`, `example.b`, but not `example.0`)
///
/// @sample A language filter that applies to typescript files on disk: `{ language: 'typescript',
/// scheme: 'file' }`
/// @sample A language filter that applies to all package.json paths: `{ language: 'json', pattern:
/// '**package.json' }`
///
/// @since 3.17.0
using TextDocumentFilter =
    rfl::Variant<TextDocumentFilterLanguage, TextDocumentFilterScheme, TextDocumentFilterPattern>;

/// Symbol tags are extra annotations that tweak the rendering of a symbol.
///
/// @since 3.16
enum class SymbolTag : uint8_t {
    Deprecated = 1,
};

/// A symbol kind.
enum class SymbolKind : uint8_t {
    File = 1,
    Module = 2,
    Namespace = 3,
    Package = 4,
    Class = 5,
    Method = 6,
    Property = 7,
    Field = 8,
    Constructor = 9,
    Enum = 10,
    Interface = 11,
    Function = 12,
    Variable = 13,
    Constant = 14,
    String = 15,
    Number = 16,
    Boolean = 17,
    Array = 18,
    Object = 19,
    Key = 20,
    Null = 21,
    EnumMember = 22,
    Struct = 23,
    Event = 24,
    Operator = 25,
    TypeParameter = 26,
};

/// A notebook cell text document filter denotes a cell text
/// document by different properties.
///
/// @since 3.17.0
struct NotebookCellTextDocumentFilter {
    /// A filter that matches against the notebook
    /// containing the notebook cell. If a string
    /// value is provided it matches against the
    /// notebook type. '*' matches every notebook.
    rfl::Variant<std::string, NotebookDocumentFilter> notebook;

    /// A language id like `python`.
    ///
    /// Will be matched against the language id of the
    /// notebook cell document. '*' matches every language.
    std::optional<std::string> language;
};

/// A notebook cell kind.
///
/// @since 3.17.0
enum class NotebookCellKind : uint8_t {
    Markup = 1,
    Code = 2,
};

/// Describes the content type that a client supports in various
/// result literals like `Hover`, `ParameterInfo` or `CompletionItem`.
///
/// Please note that `MarkupKinds` must not start with a `$`. This kinds
/// are reserved for internal usage.
using MarkupKind = rfl::Literal<"plaintext", "markdown">;

/// Represents a location inside a resource, such as a line
/// inside a text file.
struct Location {
    URI uri;

    Range range;
};

/// A set of predefined range kinds.
using FoldingRangeKind = rfl::Literal<"comment", "imports", "region">;

/// A pattern to describe in which file operation requests or notifications
/// the server is interested in receiving.
///
/// @since 3.16.0
struct FileOperationPattern {
    /// The glob pattern to match. Glob patterns can have the following syntax:
    /// - `*` to match one or more characters in a path segment
    /// - `?` to match on one character in a path segment
    /// - `**` to match any number of path segments, including none
    /// - `{}` to group sub patterns into an OR expression. (e.g. `**​/*.{ts,js}` matches all
    /// TypeScript and JavaScript files)
    /// - `[]` to declare a range of characters to match in a path segment (e.g., `example.[0-9]` to
    /// match on `example.0`, `example.1`, …)
    /// - `[!...]` to negate a range of characters to match in a path segment (e.g.,
    /// `example.[!0-9]` to match on `example.a`, `example.b`, but not `example.0`)
    std::string glob;

    /// Whether to match files or folders with this pattern.
    ///
    /// Matches both if undefined.
    std::optional<FileOperationPatternKind> matches;

    /// Additional options used during matching.
    std::optional<FileOperationPatternOptions> options;
};

struct ExecutionSummary {
    /// A strict monotonically increasing value
    /// indicating the execution order of a cell
    /// inside a notebook.
    uint executionOrder;

    /// Whether the execution was successful or
    /// not if known by the client.
    std::optional<bool> success;
};

/// @since 3.18.0
struct CompletionItemTagOptions {
    /// The tags supported by the client.
    std::vector<CompletionItemTag> valueSet;
};

/// The kind of a completion entry.
enum class CompletionItemKind : uint8_t {
    Text = 1,
    Method = 2,
    Function = 3,
    Constructor = 4,
    Field = 5,
    Variable = 6,
    Class = 7,
    Interface = 8,
    Module = 9,
    Property = 10,
    Unit = 11,
    Value = 12,
    Enum = 13,
    Keyword = 14,
    Snippet = 15,
    Color = 16,
    File = 17,
    Reference = 18,
    Folder = 19,
    EnumMember = 20,
    Constant = 21,
    Struct = 22,
    Event = 23,
    Operator = 24,
    TypeParameter = 25,
};

/// Code action tags are extra annotations that tweak the behavior of a code action.
///
/// @since 3.18.0 - proposed
enum class CodeActionTag : uint8_t {
    LLMGenerated = 1,
};

/// @since 3.18.0
struct ClientSignatureParameterInformationOptions {
    /// The client supports processing label offsets instead of a
    /// simple label string.
    ///
    /// @since 3.14.0
    std::optional<bool> labelOffsetSupport;
};

/// @since 3.18.0
struct ClientSemanticTokensRequestFullDelta {
    /// The client will send the `textDocument/semanticTokens/full/delta` request if
    /// the server provides a corresponding handler.
    std::optional<bool> delta;
};

/// @since 3.18.0
struct ClientDiagnosticsTagOptions {
    /// The tags supported by the client.
    std::vector<DiagnosticTag> valueSet;
};

/// @since 3.18.0
struct ClientCompletionItemResolveOptions {
    /// The properties that a client can resolve lazily.
    std::vector<std::string> properties;
};

/// @since 3.18.0
struct ClientCompletionItemInsertTextModeOptions {
    std::vector<InsertTextMode> valueSet;
};

/// @since 3.18.0
struct ClientCodeActionKindOptions {
    /// The code action kind values the client supports. When this
    /// property exists the client also guarantees that it will
    /// handle values outside its set gracefully and falls back
    /// to a default value when unknown.
    std::vector<CodeActionKind> valueSet;
};

using TokenFormat = rfl::Literal<"relative">;

/// A literal to identify a text document in the client.
struct TextDocumentIdentifier {
    /// The text document's uri.
    URI uri;
};

/// @since 3.18.0
struct TextDocumentContentChangeWholeDocument {
    /// The new text of the whole document.
    std::string text;
};

/// @since 3.18.0
struct TextDocumentContentChangePartial {
    /// The range of the document that changed.
    Range range;

    /// The optional length of the range that got replaced.
    ///
    /// @deprecated use range instead.
    std::optional<uint> rangeLength;

    /// The new text for the provided range.
    std::string text;
};

using ResourceOperationKind = rfl::Literal<"create", "rename", "delete">;

using RegularExpressionEngineKind = std::string;

enum class PrepareSupportDefaultBehavior : uint8_t {
    Identifier = 1,
};

/// @since 3.18.0
struct NotebookCellLanguage {
    std::string language;
};

/// A notebook cell.
///
/// A cell's document URI must be unique across ALL notebook
/// cells and can therefore be used to uniquely identify a
/// notebook cell or the cell's text document.
///
/// @since 3.17.0
struct NotebookCell {
    /// The cell's kind
    NotebookCellKind kind;

    /// The URI of the cell's text document
    /// content.
    URI document;

    /// Additional metadata stored with the cell.
    ///
    /// Note: should always be an object literal (e.g. LSPObject)
    std::optional<LSPObject> metadata;

    /// Additional execution summary information
    /// if supported by the client.
    std::optional<ExecutionSummary> executionSummary;
};

/// A `MarkupContent` literal represents a string value which content is interpreted base on its
/// kind flag. Currently the protocol supports `plaintext` and `markdown` as markup kinds.
///
/// If the kind is `markdown` then the value can contain fenced code blocks like in GitHub issues.
/// See https://help.github.com/articles/creating-and-highlighting-code-blocks/#syntax-highlighting
///
/// Here is an example how such a string can be constructed using JavaScript / TypeScript:
/// ```ts
/// let markdown: MarkdownContent = {
///  kind: MarkupKind.Markdown,
///  value: [
///    '# Header',
///    'Some text',
///    '```typescript',
///    'someCode();',
///    '```'
///  ].join('\n')
/// };
/// ```
///
/// *Please Note* that clients might sanitize the return markdown. A client could decide to
/// remove HTML from the markdown to avoid script execution.
struct MarkupContent {
    /// The type of the Markup
    MarkupKind kind;

    /// The content itself
    std::string value;
};

/// Predefined Language kinds
/// @since 3.18.0
/// @proposed
using LanguageKind =
    rfl::Literal<"abap", "bat", "bibtex", "clojure", "coffeescript", "c", "cpp", "csharp", "css",
                 "diff", "dart", "dockerfile", "elixir", "erlang", "fsharp", "git-commit", "rebase",
                 "go", "groovy", "handlebars", "haskell", "html", "ini", "java", "javascript",
                 "javascriptreact", "json", "latex", "less", "lua", "makefile", "markdown",
                 "objective-c", "objective-cpp", "perl", "perl6", "php", "powershell", "jade",
                 "python", "r", "razor", "ruby", "rust", "scss", "sass", "scala", "shaderlab",
                 "shellscript", "sql", "swift", "systemverilog", "systemverilogheader", "verilog",
                 "typescript", "typescriptreact", "tex", "vb", "xml", "xsl", "yaml">;

/// A filter to describe in which file operation requests or notifications
/// the server is interested in receiving.
///
/// @since 3.16.0
struct FileOperationFilter {
    /// A Uri scheme like `file` or `untitled`.
    std::optional<std::string> scheme;

    /// The actual file operation pattern.
    FileOperationPattern pattern;
};

using FailureHandlingKind = rfl::Literal<"abort", "transactional", "textOnlyTransactional", "undo">;

/// A document filter describes a top level text document or
/// a notebook cell document.
///
/// @since 3.17.0 - support for NotebookCellTextDocumentFilter.
using DocumentFilter = rfl::Variant<TextDocumentFilter, NotebookCellTextDocumentFilter>;

/// General diagnostics capabilities for pull and push model.
struct DiagnosticsCapabilities {
    /// Whether the clients accepts diagnostics with related information.
    std::optional<bool> relatedInformation;

    /// Client supports the tag property to provide meta data about a diagnostic.
    /// Clients supporting tags have to handle unknown tags gracefully.
    ///
    /// @since 3.15.0
    std::optional<ClientDiagnosticsTagOptions> tagSupport;

    /// Client supports a codeDescription property
    ///
    /// @since 3.16.0
    std::optional<bool> codeDescriptionSupport;

    /// Whether code action supports the `data` property which is
    /// preserved between a `textDocument/publishDiagnostics` and
    /// `textDocument/codeAction` request.
    ///
    /// @since 3.16.0
    std::optional<bool> dataSupport;
};

/// The diagnostic's severity.
enum class DiagnosticSeverity : uint8_t {
    Error = 1,
    Warning = 2,
    Information = 3,
    Hint = 4,
};

/// Represents a related message and source code location for a diagnostic. This should be
/// used to point to code locations that cause or related to a diagnostics, e.g when duplicating
/// a symbol in a scope.
struct DiagnosticRelatedInformation {
    /// The location of this related diagnostic information.
    Location location;

    /// The message of this related diagnostic information.
    std::string message;
};

/// The client supports the following `CompletionList` specific
/// capabilities.
///
/// @since 3.17.0
struct CompletionListCapabilities {
    /// The client supports the following itemDefaults on
    /// a completion list.
    ///
    /// The value lists the supported property names of the
    /// `CompletionList.itemDefaults` object. If omitted
    /// no properties are supported.
    ///
    /// @since 3.17.0
    std::optional<std::vector<std::string>> itemDefaults;

    /// Specifies whether the client supports `CompletionList.applyKind` to
    /// indicate how supported values from `completionList.itemDefaults`
    /// and `completion` will be combined.
    ///
    /// If a client supports `applyKind` it must support it for all fields
    /// that it supports that are listed in `CompletionList.applyKind`. This
    /// means when clients add support for new/future fields in completion
    /// items the MUST also support merge for them if those fields are
    /// defined in `CompletionList.applyKind`.
    ///
    /// @since 3.18.0
    std::optional<bool> applyKindSupport;
};

/// Structure to capture a description for an error code.
///
/// @since 3.16.0
struct CodeDescription {
    /// An URI to open with more information about the diagnostic error.
    URI href;
};

/// @since 3.18.0 - proposed
struct CodeActionTagOptions {
    /// The tags supported by the client.
    std::vector<CodeActionTag> valueSet;
};

/// @since 3.18.0
struct ClientSymbolTagOptions {
    /// The tags supported by the client.
    std::vector<SymbolTag> valueSet;
};

/// @since 3.18.0
struct ClientSymbolResolveOptions {
    /// The properties that a client can resolve lazily. Usually
    /// `location.range`
    std::vector<std::string> properties;
};

/// @since 3.18.0
struct ClientSymbolKindOptions {
    /// The symbol kind values the client supports. When this
    /// property exists the client also guarantees that it will
    /// handle values outside its set gracefully and falls back
    /// to a default value when unknown.
    ///
    /// If this property is not present the client only supports
    /// the symbol kinds from `File` to `Array` as defined in
    /// the initial version of the protocol.
    std::optional<std::vector<SymbolKind>> valueSet;
};

/// @since 3.18.0
struct ClientSignatureInformationOptions {
    /// Client supports the following content formats for the documentation
    /// property. The order describes the preferred format of the client.
    std::optional<std::vector<MarkupKind>> documentationFormat;

    /// Client capabilities specific to parameter information.
    std::optional<ClientSignatureParameterInformationOptions> parameterInformation;

    /// The client supports the `activeParameter` property on `SignatureInformation`
    /// literal.
    ///
    /// @since 3.16.0
    std::optional<bool> activeParameterSupport;

    /// The client supports the `activeParameter` property on
    /// `SignatureHelp`/`SignatureInformation` being set to `null` to
    /// indicate that no parameter should be active.
    ///
    /// @since 3.18.0
    /// @proposed
    std::optional<bool> noActiveParameterSupport;
};

/// @since 3.18.0
struct ClientShowMessageActionItemOptions {
    /// Whether the client supports additional attributes which
    /// are preserved and send back to the server in the
    /// request's response.
    std::optional<bool> additionalPropertiesSupport;
};

/// @since 3.18.0
struct ClientSemanticTokensRequestOptions {
    /// The client will send the `textDocument/semanticTokens/range` request if
    /// the server provides a corresponding handler.
    std::optional<bool> range;

    /// The client will send the `textDocument/semanticTokens/full` request if
    /// the server provides a corresponding handler.
    std::optional<rfl::Variant<bool, ClientSemanticTokensRequestFullDelta>> full;
};

/// @since 3.18.0
struct ClientInlayHintResolveOptions {
    /// The properties that a client can resolve lazily.
    std::vector<std::string> properties;
};

/// @since 3.18.0
struct ClientFoldingRangeOptions {
    /// If set, the client signals that it supports setting collapsedText on
    /// folding ranges to display custom labels instead of the default text.
    ///
    /// @since 3.17.0
    std::optional<bool> collapsedText;
};

/// @since 3.18.0
struct ClientFoldingRangeKindOptions {
    /// The folding range kind values the client supports. When this
    /// property exists the client also guarantees that it will
    /// handle values outside its set gracefully and falls back
    /// to a default value when unknown.
    std::optional<std::vector<FoldingRangeKind>> valueSet;
};

/// @since 3.18.0
struct ClientCompletionItemOptionsKind {
    /// The completion item kind values the client supports. When this
    /// property exists the client also guarantees that it will
    /// handle values outside its set gracefully and falls back
    /// to a default value when unknown.
    ///
    /// If this property is not present the client only supports
    /// the completion items kinds from `Text` to `Reference` as defined in
    /// the initial version of the protocol.
    std::optional<std::vector<CompletionItemKind>> valueSet;
};

/// @since 3.18.0
struct ClientCompletionItemOptions {
    /// Client supports snippets as insert text.
    ///
    /// A snippet can define tab stops and placeholders with `$1`, `$2`
    /// and `${3:foo}`. `$0` defines the final tab stop, it defaults to
    /// the end of the snippet. Placeholders with equal identifiers are linked,
    /// that is typing in one will update others too.
    std::optional<bool> snippetSupport;

    /// Client supports commit characters on a completion item.
    std::optional<bool> commitCharactersSupport;

    /// Client supports the following content formats for the documentation
    /// property. The order describes the preferred format of the client.
    std::optional<std::vector<MarkupKind>> documentationFormat;

    /// Client supports the deprecated property on a completion item.
    std::optional<bool> deprecatedSupport;

    /// Client supports the preselect property on a completion item.
    std::optional<bool> preselectSupport;

    /// Client supports the tag property on a completion item. Clients supporting
    /// tags have to handle unknown tags gracefully. Clients especially need to
    /// preserve unknown tags when sending a completion item back to the server in
    /// a resolve call.
    ///
    /// @since 3.15.0
    std::optional<CompletionItemTagOptions> tagSupport;

    /// Client support insert replace edit to control different behavior if a
    /// completion item is inserted in the text or should replace text.
    ///
    /// @since 3.16.0
    std::optional<bool> insertReplaceSupport;

    /// Indicates which properties a client can resolve lazily on a completion
    /// item. Before version 3.16.0 only the predefined properties `documentation`
    /// and `details` could be resolved lazily.
    ///
    /// @since 3.16.0
    std::optional<ClientCompletionItemResolveOptions> resolveSupport;

    /// The client supports the `insertTextMode` property on
    /// a completion item to override the whitespace handling mode
    /// as defined by the client (see `insertTextMode`).
    ///
    /// @since 3.16.0
    std::optional<ClientCompletionItemInsertTextModeOptions> insertTextModeSupport;

    /// The client has support for completion item label
    /// details (see also `CompletionItemLabelDetails`).
    ///
    /// @since 3.17.0
    std::optional<bool> labelDetailsSupport;
};

/// @since 3.18.0
struct ClientCodeLensResolveOptions {
    /// The properties that a client can resolve lazily.
    std::vector<std::string> properties;
};

/// @since 3.18.0
struct ClientCodeActionResolveOptions {
    /// The properties that a client can resolve lazily.
    std::vector<std::string> properties;
};

/// @since 3.18.0
struct ClientCodeActionLiteralOptions {
    /// The code action kind is support with the following value
    /// set.
    ClientCodeActionKindOptions codeActionKind;
};

/// @since 3.18.0
struct ChangeAnnotationsSupportOptions {
    /// Whether the client groups edits with equal labels into tree nodes,
    /// for instance all edits labelled with "Changes in Strings" would
    /// be a tree node.
    std::optional<bool> groupsOnLabel;
};

/// Client capabilities for a {@link WorkspaceSymbolRequest}.
struct WorkspaceSymbolClientCapabilities {
    /// Symbol request supports dynamic registration.
    std::optional<bool> dynamicRegistration;

    /// Specific capabilities for the `SymbolKind` in the `workspace/symbol` request.
    std::optional<ClientSymbolKindOptions> symbolKind;

    /// The client supports tags on `SymbolInformation`.
    /// Clients supporting tags have to handle unknown tags gracefully.
    ///
    /// @since 3.16.0
    std::optional<ClientSymbolTagOptions> tagSupport;

    /// The client support partial workspace symbols. The client will send the
    /// request `workspaceSymbol/resolve` to the server to resolve additional
    /// properties.
    ///
    /// @since 3.17.0
    std::optional<ClientSymbolResolveOptions> resolveSupport;
};

struct WorkspaceEditClientCapabilities {
    /// The client supports versioned document changes in `WorkspaceEdit`s
    std::optional<bool> documentChanges;

    /// The resource operations the client supports. Clients should at least
    /// support 'create', 'rename' and 'delete' files and folders.
    ///
    /// @since 3.13.0
    std::optional<std::vector<ResourceOperationKind>> resourceOperations;

    /// The failure handling strategy of a client if applying the workspace edit
    /// fails.
    ///
    /// @since 3.13.0
    std::optional<FailureHandlingKind> failureHandling;

    /// Whether the client normalizes line endings to the client specific
    /// setting.
    /// If set to `true` the client will normalize line ending characters
    /// in a workspace edit to the client-specified new line
    /// character.
    ///
    /// @since 3.16.0
    std::optional<bool> normalizesLineEndings;

    /// Whether the client in general supports change annotations on text edits,
    /// create file, rename file and delete file changes.
    ///
    /// @since 3.16.0
    std::optional<ChangeAnnotationsSupportOptions> changeAnnotationSupport;

    /// Whether the client supports `WorkspaceEditMetadata` in `WorkspaceEdit`s.
    ///
    /// @since 3.18.0
    /// @proposed
    std::optional<bool> metadataSupport;

    /// Whether the client supports snippets as text edits.
    ///
    /// @since 3.18.0
    /// @proposed
    std::optional<bool> snippetEditSupport;
};

struct WorkDoneProgressOptions {
    std::optional<bool> workDoneProgress;
};

/// A text document identifier to denote a specific version of a text document.
struct VersionedTextDocumentIdentifier {
    /// The version number of this document.
    int version;

    /// @inherited from TextDocumentIdentifier
    /// The text document's uri.
    URI uri;
};

/// @since 3.17.0
struct TypeHierarchyClientCapabilities {
    /// Whether implementation supports dynamic registration. If this is set to `true`
    /// the client supports the new `(TextDocumentRegistrationOptions & StaticRegistrationOptions)`
    /// return value for the corresponding server capability as well.
    std::optional<bool> dynamicRegistration;
};

/// Since 3.6.0
struct TypeDefinitionClientCapabilities {
    /// Whether implementation supports dynamic registration. If this is set to `true`
    /// the client supports the new `TypeDefinitionRegistrationOptions` return value
    /// for the corresponding server capability as well.
    std::optional<bool> dynamicRegistration;

    /// The client supports additional metadata in the form of definition links.
    ///
    /// Since 3.14.0
    std::optional<bool> linkSupport;
};

/// A text edit applicable to a text document.
struct TextEdit {
    /// The range of the text document to be manipulated. To insert
    /// text into a document create a range where start === end.
    Range range;

    /// The string to be inserted. For delete operations use an
    /// empty string.
    std::string newText;
};

struct TextDocumentSyncClientCapabilities {
    /// Whether text document synchronization supports dynamic registration.
    std::optional<bool> dynamicRegistration;

    /// The client supports sending will save notifications.
    std::optional<bool> willSave;

    /// The client supports sending a will save request and
    /// waits for a response providing text edits which will
    /// be applied to the document before it is saved.
    std::optional<bool> willSaveWaitUntil;

    /// The client supports did save notifications.
    std::optional<bool> didSave;
};

/// An item to transfer a text document from the client to the
/// server.
struct TextDocumentItem {
    /// The text document's uri.
    URI uri;

    /// The text document's language identifier.
    LanguageKind languageId;

    /// The version number of this document (it will increase after each
    /// change, including undo/redo).
    int version;

    /// The content of the opened text document.
    std::string text;
};

struct TextDocumentFilterClientCapabilities {
    /// The client supports Relative Patterns.
    ///
    /// @since 3.18.0
    std::optional<bool> relativePatternSupport;
};

/// Text document content provider options.
///
/// @since 3.18.0
/// @proposed
struct TextDocumentContentOptions {
    /// The schemes for which the server provides content.
    std::vector<std::string> schemes;
};

/// Client capabilities for a text document content provider.
///
/// @since 3.18.0
/// @proposed
struct TextDocumentContentClientCapabilities {
    /// Text document content provider supports dynamic registration.
    std::optional<bool> dynamicRegistration;
};

/// An event describing a change to a text document. If only a text is provided
/// it is considered to be the full content of the document.
using TextDocumentContentChangeEvent =
    rfl::Variant<TextDocumentContentChangePartial, TextDocumentContentChangeWholeDocument>;

/// A string value used as a snippet is a template which allows to insert text
/// and to control the editor cursor when insertion happens.
///
/// A snippet can define tab stops and placeholders with `$1`, `$2`
/// and `${3:foo}`. `$0` defines the final tab stop, it defaults to
/// the end of the snippet. Variables are defined with `$name` and
/// `${name:default value}`.
///
/// @since 3.18.0
/// @proposed
struct StringValue {
    /// The kind of string value.
    rfl::Literal<"snippet"> kind;

    /// The snippet string.
    std::string value;
};

/// Static registration options to be returned in the initialize
/// request.
struct StaticRegistrationOptions {
    /// The id used to register the request. The id can be used to deregister
    /// the request again. See also Registration#id.
    std::optional<std::string> id;
};

/// @since 3.18.0
struct StaleRequestSupportOptions {
    /// The client will actively cancel the request.
    bool cancel;

    /// The list of requests for which the client
    /// will retry the request if it receives a
    /// response with error code `ContentModified`
    std::vector<std::string> retryOnContentModified;
};

/// Client Capabilities for a {@link SignatureHelpRequest}.
struct SignatureHelpClientCapabilities {
    /// Whether signature help supports dynamic registration.
    std::optional<bool> dynamicRegistration;

    /// The client supports the following `SignatureInformation`
    /// specific properties.
    std::optional<ClientSignatureInformationOptions> signatureInformation;

    /// The client supports to send additional context information for a
    /// `textDocument/signatureHelp` request. A client that opts into
    /// contextSupport will also support the `retriggerCharacters` on
    /// `SignatureHelpOptions`.
    ///
    /// @since 3.15.0
    std::optional<bool> contextSupport;
};

/// Show message request client capabilities
struct ShowMessageRequestClientCapabilities {
    /// Capabilities specific to the `MessageActionItem` type.
    std::optional<ClientShowMessageActionItemOptions> messageActionItem;
};

/// Client capabilities for the showDocument request.
///
/// @since 3.16.0
struct ShowDocumentClientCapabilities {
    /// The client has support for the showDocument
    /// request.
    bool support;
};

/// @since 3.16.0
struct SemanticTokensWorkspaceClientCapabilities {
    /// Whether the client implementation supports a refresh request sent from
    /// the server to the client.
    ///
    /// Note that this event is global and will force the client to refresh all
    /// semantic tokens currently shown. It should be used with absolute care
    /// and is useful for situation where a server for example detects a project
    /// wide change that requires such a calculation.
    std::optional<bool> refreshSupport;
};

/// @since 3.16.0
struct SemanticTokensLegend {
    /// The token types a server uses.
    std::vector<std::string> tokenTypes;

    /// The token modifiers a server uses.
    std::vector<std::string> tokenModifiers;
};

/// Semantic tokens options to support deltas for full documents
///
/// @since 3.18.0
struct SemanticTokensFullDelta {
    /// The server supports deltas for full documents.
    std::optional<bool> delta;
};

/// @since 3.16.0
struct SemanticTokensClientCapabilities {
    /// Whether implementation supports dynamic registration. If this is set to `true`
    /// the client supports the new `(TextDocumentRegistrationOptions & StaticRegistrationOptions)`
    /// return value for the corresponding server capability as well.
    std::optional<bool> dynamicRegistration;

    /// Which requests the client supports and might send to the server
    /// depending on the server's capability. Please note that clients might not
    /// show semantic tokens or degrade some of the user experience if a range
    /// or full request is advertised by the client but not provided by the
    /// server. If for example the client capability `requests.full` and
    /// `request.range` are both set to true but the server only provides a
    /// range provider the client might not render a minimap correctly or might
    /// even decide to not show any semantic tokens at all.
    ClientSemanticTokensRequestOptions requests;

    /// The token types that the client supports.
    std::vector<std::string> tokenTypes;

    /// The token modifiers that the client supports.
    std::vector<std::string> tokenModifiers;

    /// The token formats the clients supports.
    std::vector<TokenFormat> formats;

    /// Whether the client supports tokens that can overlap each other.
    std::optional<bool> overlappingTokenSupport;

    /// Whether the client supports tokens that can span multiple lines.
    std::optional<bool> multilineTokenSupport;

    /// Whether the client allows the server to actively cancel a
    /// semantic token request, e.g. supports returning
    /// LSPErrorCodes.ServerCancelled. If a server does the client
    /// needs to retrigger the request.
    ///
    /// @since 3.17.0
    std::optional<bool> serverCancelSupport;

    /// Whether the client uses semantic tokens to augment existing
    /// syntax tokens. If set to `true` client side created syntax
    /// tokens and semantic tokens are both used for colorization. If
    /// set to `false` the client only uses the returned semantic tokens
    /// for colorization.
    ///
    /// If the value is `undefined` then the client behavior is not
    /// specified.
    ///
    /// @since 3.17.0
    std::optional<bool> augmentsSyntaxTokens;
};

struct SelectionRangeClientCapabilities {
    /// Whether implementation supports dynamic registration for selection range providers. If this
    /// is set to `true` the client supports the new `SelectionRangeRegistrationOptions` return
    /// value for the corresponding server capability as well.
    std::optional<bool> dynamicRegistration;
};

struct RenameClientCapabilities {
    /// Whether rename supports dynamic registration.
    std::optional<bool> dynamicRegistration;

    /// Client supports testing for validity of rename operations
    /// before execution.
    ///
    /// @since 3.12.0
    std::optional<bool> prepareSupport;

    /// Client supports the default behavior result.
    ///
    /// The value indicates the default behavior used by the
    /// client.
    ///
    /// @since 3.16.0
    std::optional<PrepareSupportDefaultBehavior> prepareSupportDefaultBehavior;

    /// Whether the client honors the change annotations in
    /// text edits and resource operations returned via the
    /// rename request's workspace edit by for example presenting
    /// the workspace edit in the user interface and asking
    /// for confirmation.
    ///
    /// @since 3.16.0
    std::optional<bool> honorsChangeAnnotations;
};

/// Client capabilities specific to regular expressions.
///
/// @since 3.16.0
struct RegularExpressionsClientCapabilities {
    /// The engine's name.
    RegularExpressionEngineKind engine;

    /// The engine's version.
    std::optional<std::string> version;
};

/// Client Capabilities for a {@link ReferencesRequest}.
struct ReferenceClientCapabilities {
    /// Whether references supports dynamic registration.
    std::optional<bool> dynamicRegistration;
};

/// The publish diagnostic client capabilities.
struct PublishDiagnosticsClientCapabilities {
    /// Whether the client interprets the version property of the
    /// `textDocument/publishDiagnostics` notification's parameter.
    ///
    /// @since 3.15.0
    std::optional<bool> versionSupport;

    /// @inherited from DiagnosticsCapabilities
    /// Whether the clients accepts diagnostics with related information.
    std::optional<bool> relatedInformation;

    /// @inherited from DiagnosticsCapabilities
    /// Client supports the tag property to provide meta data about a diagnostic.
    /// Clients supporting tags have to handle unknown tags gracefully.
    ///
    /// @since 3.15.0
    std::optional<ClientDiagnosticsTagOptions> tagSupport;

    /// @inherited from DiagnosticsCapabilities
    /// Client supports a codeDescription property
    ///
    /// @since 3.16.0
    std::optional<bool> codeDescriptionSupport;

    /// @inherited from DiagnosticsCapabilities
    /// Whether code action supports the `data` property which is
    /// preserved between a `textDocument/publishDiagnostics` and
    /// `textDocument/codeAction` request.
    ///
    /// @since 3.16.0
    std::optional<bool> dataSupport;
};

/// A set of predefined position encoding kinds.
///
/// @since 3.17.0
using PositionEncodingKind = rfl::Literal<"utf-8", "utf-16", "utf-32">;

/// Represents a parameter of a callable-signature. A parameter can
/// have a label and a doc-comment.
struct ParameterInformation {
    /// The label of this parameter information.
    ///
    /// Either a string or an inclusive start and exclusive end offsets within its containing
    /// signature label. (see SignatureInformation.label). The offsets are based on a UTF-16
    /// string representation as `Position` and `Range` does.
    ///
    /// To avoid ambiguities a server should use the [start, end] offset value instead of using
    /// a substring. Whether a client support this is controlled via `labelOffsetSupport` client
    /// capability.
    ///
    /// *Note*: a label of type string should be a substring of its containing signature label.
    /// Its intended use case is to highlight the parameter label part in the
    /// `SignatureInformation.label`.
    rfl::Variant<std::string, rfl::Tuple<uint, uint>> label;

    /// The human-readable doc-comment of this parameter. Will be shown
    /// in the UI but can be omitted.
    std::optional<rfl::Variant<std::string, MarkupContent>> documentation;
};

/// Notebook specific client capabilities.
///
/// @since 3.17.0
struct NotebookDocumentSyncClientCapabilities {
    /// Whether implementation supports dynamic registration. If this is
    /// set to `true` the client supports the new
    /// `(TextDocumentRegistrationOptions & StaticRegistrationOptions)`
    /// return value for the corresponding server capability as well.
    std::optional<bool> dynamicRegistration;

    /// The client supports sending execution summary data per cell.
    std::optional<bool> executionSummarySupport;
};

/// @since 3.18.0
struct NotebookDocumentFilterWithNotebook {
    /// The notebook to be synced If a string
    /// value is provided it matches against the
    /// notebook type. '*' matches every notebook.
    rfl::Variant<std::string, NotebookDocumentFilter> notebook;

    /// The cells of the matching notebook to be synced.
    std::optional<std::vector<NotebookCellLanguage>> cells;
};

/// @since 3.18.0
struct NotebookDocumentFilterWithCells {
    /// The notebook to be synced If a string
    /// value is provided it matches against the
    /// notebook type. '*' matches every notebook.
    std::optional<rfl::Variant<std::string, NotebookDocumentFilter>> notebook;

    /// The cells of the matching notebook to be synced.
    std::vector<NotebookCellLanguage> cells;
};

/// A change describing how to move a `NotebookCell`
/// array from state S to S'.
///
/// @since 3.17.0
struct NotebookCellArrayChange {
    /// The start oftest of the cell that changed.
    uint start;

    /// The deleted cells
    uint deleteCount;

    /// The new cells, if any
    std::optional<std::vector<NotebookCell>> cells;
};

/// Client capabilities specific to the moniker request.
///
/// @since 3.16.0
struct MonikerClientCapabilities {
    /// Whether moniker supports dynamic registration. If this is set to `true`
    /// the client supports the new `MonikerRegistrationOptions` return value
    /// for the corresponding server capability as well.
    std::optional<bool> dynamicRegistration;
};

/// Client capabilities specific to the used markdown parser.
///
/// @since 3.16.0
struct MarkdownClientCapabilities {
    /// The name of the parser.
    std::string parser;

    /// The version of the parser.
    std::optional<std::string> version;

    /// A list of HTML tags that the client allows / supports in
    /// Markdown.
    ///
    /// @since 3.17.0
    std::optional<std::vector<std::string>> allowedTags;
};

/// Client capabilities for the linked editing range request.
///
/// @since 3.16.0
struct LinkedEditingRangeClientCapabilities {
    /// Whether implementation supports dynamic registration. If this is set to `true`
    /// the client supports the new `(TextDocumentRegistrationOptions & StaticRegistrationOptions)`
    /// return value for the corresponding server capability as well.
    std::optional<bool> dynamicRegistration;
};

/// Client workspace capabilities specific to inline values.
///
/// @since 3.17.0
struct InlineValueWorkspaceClientCapabilities {
    /// Whether the client implementation supports a refresh request sent from the
    /// server to the client.
    ///
    /// Note that this event is global and will force the client to refresh all
    /// inline values currently shown. It should be used with absolute care and is
    /// useful for situation where a server for example detects a project wide
    /// change that requires such a calculation.
    std::optional<bool> refreshSupport;
};

/// Client capabilities specific to inline values.
///
/// @since 3.17.0
struct InlineValueClientCapabilities {
    /// Whether implementation supports dynamic registration for inline value providers.
    std::optional<bool> dynamicRegistration;
};

/// Client capabilities specific to inline completions.
///
/// @since 3.18.0
/// @proposed
struct InlineCompletionClientCapabilities {
    /// Whether implementation supports dynamic registration for inline completion providers.
    std::optional<bool> dynamicRegistration;
};

/// Client workspace capabilities specific to inlay hints.
///
/// @since 3.17.0
struct InlayHintWorkspaceClientCapabilities {
    /// Whether the client implementation supports a refresh request sent from
    /// the server to the client.
    ///
    /// Note that this event is global and will force the client to refresh all
    /// inlay hints currently shown. It should be used with absolute care and
    /// is useful for situation where a server for example detects a project wide
    /// change that requires such a calculation.
    std::optional<bool> refreshSupport;
};

/// Inlay hint client capabilities.
///
/// @since 3.17.0
struct InlayHintClientCapabilities {
    /// Whether inlay hints support dynamic registration.
    std::optional<bool> dynamicRegistration;

    /// Indicates which properties a client can resolve lazily on an inlay
    /// hint.
    std::optional<ClientInlayHintResolveOptions> resolveSupport;
};

/// @since 3.6.0
struct ImplementationClientCapabilities {
    /// Whether implementation supports dynamic registration. If this is set to `true`
    /// the client supports the new `ImplementationRegistrationOptions` return value
    /// for the corresponding server capability as well.
    std::optional<bool> dynamicRegistration;

    /// The client supports additional metadata in the form of definition links.
    ///
    /// @since 3.14.0
    std::optional<bool> linkSupport;
};

struct HoverClientCapabilities {
    /// Whether hover supports dynamic registration.
    std::optional<bool> dynamicRegistration;

    /// Client supports the following content formats for the content
    /// property. The order describes the preferred format of the client.
    std::optional<std::vector<MarkupKind>> contentFormat;
};

/// Client workspace capabilities specific to folding ranges
///
/// @since 3.18.0
/// @proposed
struct FoldingRangeWorkspaceClientCapabilities {
    /// Whether the client implementation supports a refresh request sent from the
    /// server to the client.
    ///
    /// Note that this event is global and will force the client to refresh all
    /// folding ranges currently shown. It should be used with absolute care and is
    /// useful for situation where a server for example detects a project wide
    /// change that requires such a calculation.
    ///
    /// @since 3.18.0
    /// @proposed
    std::optional<bool> refreshSupport;
};

struct FoldingRangeClientCapabilities {
    /// Whether implementation supports dynamic registration for folding range
    /// providers. If this is set to `true` the client supports the new
    /// `FoldingRangeRegistrationOptions` return value for the corresponding
    /// server capability as well.
    std::optional<bool> dynamicRegistration;

    /// The maximum number of folding ranges that the client prefers to receive
    /// per document. The value serves as a hint, servers are free to follow the
    /// limit.
    std::optional<uint> rangeLimit;

    /// If set, the client signals that it only supports folding complete lines.
    /// If set, client will ignore specified `startCharacter` and `endCharacter`
    /// properties in a FoldingRange.
    std::optional<bool> lineFoldingOnly;

    /// Specific options for the folding range kind.
    ///
    /// @since 3.17.0
    std::optional<ClientFoldingRangeKindOptions> foldingRangeKind;

    /// Specific options for the folding range.
    ///
    /// @since 3.17.0
    std::optional<ClientFoldingRangeOptions> foldingRange;
};

/// The options to register for file operations.
///
/// @since 3.16.0
struct FileOperationRegistrationOptions {
    /// The actual filters.
    std::vector<FileOperationFilter> filters;
};

/// Capabilities relating to events from file operations by the user in the client.
///
/// These events do not come from the file system, they come from user operations
/// like renaming a file in the UI.
///
/// @since 3.16.0
struct FileOperationClientCapabilities {
    /// Whether the client supports dynamic registration for file requests/notifications.
    std::optional<bool> dynamicRegistration;

    /// The client has support for sending didCreateFiles notifications.
    std::optional<bool> didCreate;

    /// The client has support for sending willCreateFiles requests.
    std::optional<bool> willCreate;

    /// The client has support for sending didRenameFiles notifications.
    std::optional<bool> didRename;

    /// The client has support for sending willRenameFiles requests.
    std::optional<bool> willRename;

    /// The client has support for sending didDeleteFiles notifications.
    std::optional<bool> didDelete;

    /// The client has support for sending willDeleteFiles requests.
    std::optional<bool> willDelete;
};

/// The client capabilities of a {@link ExecuteCommandRequest}.
struct ExecuteCommandClientCapabilities {
    /// Execute command supports dynamic registration.
    std::optional<bool> dynamicRegistration;
};

/// Client Capabilities for a {@link DocumentSymbolRequest}.
struct DocumentSymbolClientCapabilities {
    /// Whether document symbol supports dynamic registration.
    std::optional<bool> dynamicRegistration;

    /// Specific capabilities for the `SymbolKind` in the
    /// `textDocument/documentSymbol` request.
    std::optional<ClientSymbolKindOptions> symbolKind;

    /// The client supports hierarchical document symbols.
    std::optional<bool> hierarchicalDocumentSymbolSupport;

    /// The client supports tags on `SymbolInformation`. Tags are supported on
    /// `DocumentSymbol` if `hierarchicalDocumentSymbolSupport` is set to true.
    /// Clients supporting tags have to handle unknown tags gracefully.
    ///
    /// @since 3.16.0
    std::optional<ClientSymbolTagOptions> tagSupport;

    /// The client supports an additional label presented in the UI when
    /// registering a document symbol provider.
    ///
    /// @since 3.16.0
    std::optional<bool> labelSupport;
};

/// A document selector is the combination of one or many document filters.
///
/// @sample `let sel:DocumentSelector = [{ language: 'typescript' }, { language: 'json', pattern:
/// '**∕tsconfig.json' }]`;
///
/// The use of a string as a document filter is deprecated @since 3.16.0.
using DocumentSelector = std::vector<DocumentFilter>;

/// Client capabilities of a {@link DocumentRangeFormattingRequest}.
struct DocumentRangeFormattingClientCapabilities {
    /// Whether range formatting supports dynamic registration.
    std::optional<bool> dynamicRegistration;

    /// Whether the client supports formatting multiple ranges at once.
    ///
    /// @since 3.18.0
    /// @proposed
    std::optional<bool> rangesSupport;
};

/// Client capabilities of a {@link DocumentOnTypeFormattingRequest}.
struct DocumentOnTypeFormattingClientCapabilities {
    /// Whether on type formatting supports dynamic registration.
    std::optional<bool> dynamicRegistration;
};

/// The client capabilities of a {@link DocumentLinkRequest}.
struct DocumentLinkClientCapabilities {
    /// Whether document link supports dynamic registration.
    std::optional<bool> dynamicRegistration;

    /// Whether the client supports the `tooltip` property on `DocumentLink`.
    ///
    /// @since 3.15.0
    std::optional<bool> tooltipSupport;
};

/// Client Capabilities for a {@link DocumentHighlightRequest}.
struct DocumentHighlightClientCapabilities {
    /// Whether document highlight supports dynamic registration.
    std::optional<bool> dynamicRegistration;
};

/// Client capabilities of a {@link DocumentFormattingRequest}.
struct DocumentFormattingClientCapabilities {
    /// Whether formatting supports dynamic registration.
    std::optional<bool> dynamicRegistration;
};

struct DocumentColorClientCapabilities {
    /// Whether implementation supports dynamic registration. If this is set to `true`
    /// the client supports the new `DocumentColorRegistrationOptions` return value
    /// for the corresponding server capability as well.
    std::optional<bool> dynamicRegistration;
};

struct DidChangeWatchedFilesClientCapabilities {
    /// Did change watched files notification supports dynamic registration. Please note
    /// that the current protocol doesn't support static configuration for file changes
    /// from the server side.
    std::optional<bool> dynamicRegistration;

    /// Whether the client has support for {@link  RelativePattern relative pattern}
    /// or not.
    ///
    /// @since 3.17.0
    std::optional<bool> relativePatternSupport;
};

struct DidChangeConfigurationClientCapabilities {
    /// Did change configuration notification supports dynamic registration.
    std::optional<bool> dynamicRegistration;
};

/// Workspace client capabilities specific to diagnostic pull requests.
///
/// @since 3.17.0
struct DiagnosticWorkspaceClientCapabilities {
    /// Whether the client implementation supports a refresh request sent from
    /// the server to the client.
    ///
    /// Note that this event is global and will force the client to refresh all
    /// pulled diagnostics currently shown. It should be used with absolute care and
    /// is useful for situation where a server for example detects a project wide
    /// change that requires such a calculation.
    std::optional<bool> refreshSupport;
};

/// Client capabilities specific to diagnostic pull requests.
///
/// @since 3.17.0
struct DiagnosticClientCapabilities {
    /// Whether implementation supports dynamic registration. If this is set to `true`
    /// the client supports the new `(TextDocumentRegistrationOptions & StaticRegistrationOptions)`
    /// return value for the corresponding server capability as well.
    std::optional<bool> dynamicRegistration;

    /// Whether the clients supports related documents for document diagnostic pulls.
    std::optional<bool> relatedDocumentSupport;

    /// @inherited from DiagnosticsCapabilities
    /// Whether the clients accepts diagnostics with related information.
    std::optional<bool> relatedInformation;

    /// @inherited from DiagnosticsCapabilities
    /// Client supports the tag property to provide meta data about a diagnostic.
    /// Clients supporting tags have to handle unknown tags gracefully.
    ///
    /// @since 3.15.0
    std::optional<ClientDiagnosticsTagOptions> tagSupport;

    /// @inherited from DiagnosticsCapabilities
    /// Client supports a codeDescription property
    ///
    /// @since 3.16.0
    std::optional<bool> codeDescriptionSupport;

    /// @inherited from DiagnosticsCapabilities
    /// Whether code action supports the `data` property which is
    /// preserved between a `textDocument/publishDiagnostics` and
    /// `textDocument/codeAction` request.
    ///
    /// @since 3.16.0
    std::optional<bool> dataSupport;
};

/// Represents a diagnostic, such as a compiler error or warning. Diagnostic objects
/// are only valid in the scope of a resource.
struct Diagnostic {
    /// The range at which the message applies
    Range range;

    /// The diagnostic's severity. To avoid interpretation mismatches when a
    /// server is used with different clients it is highly recommended that servers
    /// always provide a severity value.
    std::optional<DiagnosticSeverity> severity;

    /// The diagnostic's code, which usually appear in the user interface.
    std::optional<rfl::Variant<int, std::string>> code;

    /// An optional property to describe the error code.
    /// Requires the code field (above) to be present/not null.
    ///
    /// @since 3.16.0
    std::optional<CodeDescription> codeDescription;

    /// A human-readable string describing the source of this
    /// diagnostic, e.g. 'typescript' or 'super lint'. It usually
    /// appears in the user interface.
    std::optional<std::string> source;

    /// The diagnostic's message. It usually appears in the user interface
    std::string message;

    /// Additional metadata about the diagnostic.
    ///
    /// @since 3.15.0
    std::optional<std::vector<DiagnosticTag>> tags;

    /// An array of related diagnostic information, e.g. when symbol-names within
    /// a scope collide all definitions can be marked via this property.
    std::optional<std::vector<DiagnosticRelatedInformation>> relatedInformation;

    /// A data entry field that is preserved between a `textDocument/publishDiagnostics`
    /// notification and `textDocument/codeAction` request.
    ///
    /// @since 3.16.0
    std::optional<LSPAny> data;
};

/// Client Capabilities for a {@link DefinitionRequest}.
struct DefinitionClientCapabilities {
    /// Whether definition supports dynamic registration.
    std::optional<bool> dynamicRegistration;

    /// The client supports additional metadata in the form of definition links.
    ///
    /// @since 3.14.0
    std::optional<bool> linkSupport;
};

/// @since 3.14.0
struct DeclarationClientCapabilities {
    /// Whether declaration supports dynamic registration. If this is set to `true`
    /// the client supports the new `DeclarationRegistrationOptions` return value
    /// for the corresponding server capability as well.
    std::optional<bool> dynamicRegistration;

    /// The client supports additional metadata in the form of declaration links.
    std::optional<bool> linkSupport;
};

/// Completion client capabilities
struct CompletionClientCapabilities {
    /// Whether completion supports dynamic registration.
    std::optional<bool> dynamicRegistration;

    /// The client supports the following `CompletionItem` specific
    /// capabilities.
    std::optional<ClientCompletionItemOptions> completionItem;

    std::optional<ClientCompletionItemOptionsKind> completionItemKind;

    /// Defines how the client handles whitespace and indentation
    /// when accepting a completion item that uses multi line
    /// text in either `insertText` or `textEdit`.
    ///
    /// @since 3.17.0
    std::optional<InsertTextMode> insertTextMode;

    /// The client supports to send additional context information for a
    /// `textDocument/completion` request.
    std::optional<bool> contextSupport;

    /// The client supports the following `CompletionList` specific
    /// capabilities.
    ///
    /// @since 3.17.0
    std::optional<CompletionListCapabilities> completionList;
};

/// Represents a reference to a command. Provides a title which
/// will be used to represent a command in the UI and, optionally,
/// an array of arguments which will be passed to the command handler
/// function when invoked.
struct Command {
    /// Title of the command, like `save`.
    std::string title;

    /// An optional tooltip.
    ///
    /// @since 3.18.0
    /// @proposed
    std::optional<std::string> tooltip;

    /// The identifier of the actual command handler.
    std::string command;

    /// Arguments that the command handler should be
    /// invoked with.
    std::optional<std::vector<LSPAny>> arguments;
};

/// @since 3.16.0
struct CodeLensWorkspaceClientCapabilities {
    /// Whether the client implementation supports a refresh request sent from the
    /// server to the client.
    ///
    /// Note that this event is global and will force the client to refresh all
    /// code lenses currently shown. It should be used with absolute care and is
    /// useful for situation where a server for example detect a project wide
    /// change that requires such a calculation.
    std::optional<bool> refreshSupport;
};

/// The client capabilities  of a {@link CodeLensRequest}.
struct CodeLensClientCapabilities {
    /// Whether code lens supports dynamic registration.
    std::optional<bool> dynamicRegistration;

    /// Whether the client supports resolving additional code lens
    /// properties via a separate `codeLens/resolve` request.
    ///
    /// @since 3.18.0
    std::optional<ClientCodeLensResolveOptions> resolveSupport;
};

/// The Client Capabilities of a {@link CodeActionRequest}.
struct CodeActionClientCapabilities {
    /// Whether code action supports dynamic registration.
    std::optional<bool> dynamicRegistration;

    /// The client support code action literals of type `CodeAction` as a valid
    /// response of the `textDocument/codeAction` request. If the property is not
    /// set the request can only return `Command` literals.
    ///
    /// @since 3.8.0
    std::optional<ClientCodeActionLiteralOptions> codeActionLiteralSupport;

    /// Whether code action supports the `isPreferred` property.
    ///
    /// @since 3.15.0
    std::optional<bool> isPreferredSupport;

    /// Whether code action supports the `disabled` property.
    ///
    /// @since 3.16.0
    std::optional<bool> disabledSupport;

    /// Whether code action supports the `data` property which is
    /// preserved between a `textDocument/codeAction` and a
    /// `codeAction/resolve` request.
    ///
    /// @since 3.16.0
    std::optional<bool> dataSupport;

    /// Whether the client supports resolving additional code action
    /// properties via a separate `codeAction/resolve` request.
    ///
    /// @since 3.16.0
    std::optional<ClientCodeActionResolveOptions> resolveSupport;

    /// Whether the client honors the change annotations in
    /// text edits and resource operations returned via the
    /// `CodeAction#edit` property by for example presenting
    /// the workspace edit in the user interface and asking
    /// for confirmation.
    ///
    /// @since 3.16.0
    std::optional<bool> honorsChangeAnnotations;

    /// Whether the client supports documentation for a class of
    /// code actions.
    ///
    /// @since 3.18.0
    /// @proposed
    std::optional<bool> documentationSupport;

    /// Client supports the tag property on a code action. Clients
    /// supporting tags have to handle unknown tags gracefully.
    ///
    /// @since 3.18.0 - proposed
    std::optional<CodeActionTagOptions> tagSupport;
};

/// An identifier to refer to a change annotation stored with a workspace edit.
using ChangeAnnotationIdentifier = std::string;

/// @since 3.16.0
struct CallHierarchyClientCapabilities {
    /// Whether implementation supports dynamic registration. If this is set to `true`
    /// the client supports the new `(TextDocumentRegistrationOptions & StaticRegistrationOptions)`
    /// return value for the corresponding server capability as well.
    std::optional<bool> dynamicRegistration;
};

struct WorkspaceFoldersServerCapabilities {
    /// The server has support for workspace folders
    std::optional<bool> supported;

    /// Whether the server wants to receive workspace folder
    /// change notifications.
    ///
    /// If a string is provided the string is treated as an ID
    /// under which the notification is registered on the client
    /// side. The ID can be used to unregister for these events
    /// using the `client/unregisterCapability` request.
    std::optional<rfl::Variant<std::string, bool>> changeNotifications;
};

/// Workspace specific client capabilities.
struct WorkspaceClientCapabilities {
    /// The client supports applying batch edits
    /// to the workspace by supporting the request
    /// 'workspace/applyEdit'
    std::optional<bool> applyEdit;

    /// Capabilities specific to `WorkspaceEdit`s.
    std::optional<WorkspaceEditClientCapabilities> workspaceEdit;

    /// Capabilities specific to the `workspace/didChangeConfiguration` notification.
    std::optional<DidChangeConfigurationClientCapabilities> didChangeConfiguration;

    /// Capabilities specific to the `workspace/didChangeWatchedFiles` notification.
    std::optional<DidChangeWatchedFilesClientCapabilities> didChangeWatchedFiles;

    /// Capabilities specific to the `workspace/symbol` request.
    std::optional<WorkspaceSymbolClientCapabilities> symbol;

    /// Capabilities specific to the `workspace/executeCommand` request.
    std::optional<ExecuteCommandClientCapabilities> executeCommand;

    /// The client has support for workspace folders.
    ///
    /// @since 3.6.0
    std::optional<bool> workspaceFolders;

    /// The client supports `workspace/configuration` requests.
    ///
    /// @since 3.6.0
    std::optional<bool> configuration;

    /// Capabilities specific to the semantic token requests scoped to the
    /// workspace.
    ///
    /// @since 3.16.0.
    std::optional<SemanticTokensWorkspaceClientCapabilities> semanticTokens;

    /// Capabilities specific to the code lens requests scoped to the
    /// workspace.
    ///
    /// @since 3.16.0.
    std::optional<CodeLensWorkspaceClientCapabilities> codeLens;

    /// The client has support for file notifications/requests for user operations on files.
    ///
    /// Since 3.16.0
    std::optional<FileOperationClientCapabilities> fileOperations;

    /// Capabilities specific to the inline values requests scoped to the
    /// workspace.
    ///
    /// @since 3.17.0.
    std::optional<InlineValueWorkspaceClientCapabilities> inlineValue;

    /// Capabilities specific to the inlay hint requests scoped to the
    /// workspace.
    ///
    /// @since 3.17.0.
    std::optional<InlayHintWorkspaceClientCapabilities> inlayHint;

    /// Capabilities specific to the diagnostic requests scoped to the
    /// workspace.
    ///
    /// @since 3.17.0.
    std::optional<DiagnosticWorkspaceClientCapabilities> diagnostics;

    /// Capabilities specific to the folding range requests scoped to the workspace.
    ///
    /// @since 3.18.0
    /// @proposed
    std::optional<FoldingRangeWorkspaceClientCapabilities> foldingRange;

    /// Capabilities specific to the `workspace/textDocumentContent` request.
    ///
    /// @since 3.18.0
    /// @proposed
    std::optional<TextDocumentContentClientCapabilities> textDocumentContent;
};

struct WindowClientCapabilities {
    /// It indicates whether the client supports server initiated
    /// progress using the `window/workDoneProgress/create` request.
    ///
    /// The capability also controls Whether client supports handling
    /// of progress notifications. If set servers are allowed to report a
    /// `workDoneProgress` property in the request specific server
    /// capabilities.
    ///
    /// @since 3.15.0
    std::optional<bool> workDoneProgress;

    /// Capabilities specific to the showMessage request.
    ///
    /// @since 3.16.0
    std::optional<ShowMessageRequestClientCapabilities> showMessage;

    /// Capabilities specific to the showDocument request.
    ///
    /// @since 3.16.0
    std::optional<ShowDocumentClientCapabilities> showDocument;
};

/// A diagnostic report indicating that the last returned
/// report is still accurate.
///
/// @since 3.17.0
struct UnchangedDocumentDiagnosticReport {
    /// A document diagnostic report indicating
    /// no changes to the last result. A server can
    /// only return `unchanged` if result ids are
    /// provided.
    rfl::Literal<"unchanged"> kind;

    /// A result id which will be sent on the next
    /// diagnostic request for the same document.
    std::string resultId;
};

/// Type hierarchy options used during static registration.
///
/// @since 3.17.0
struct TypeHierarchyOptions {
    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

struct TypeDefinitionOptions {
    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Defines how the host (editor) should sync
/// document changes to the language server.
enum class TextDocumentSyncKind {
    None = 0,
    Full = 1,
    Incremental = 2,
};

/// General text document registration options.
struct TextDocumentRegistrationOptions {
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;
};

/// Text document content provider registration options.
///
/// @since 3.18.0
/// @proposed
struct TextDocumentContentRegistrationOptions {
    /// @inherited from TextDocumentContentOptions
    /// The schemes for which the server provides content.
    std::vector<std::string> schemes;

    /// @mixin from StaticRegistrationOptions
    /// The id used to register the request. The id can be used to deregister
    /// the request again. See also Registration#id.
    std::optional<std::string> id;
};

/// Text document specific client capabilities.
struct TextDocumentClientCapabilities {
    /// Defines which synchronization capabilities the client supports.
    std::optional<TextDocumentSyncClientCapabilities> synchronization;

    /// Defines which filters the client supports.
    ///
    /// @since 3.18.0
    std::optional<TextDocumentFilterClientCapabilities> filters;

    /// Capabilities specific to the `textDocument/completion` request.
    std::optional<CompletionClientCapabilities> completion;

    /// Capabilities specific to the `textDocument/hover` request.
    std::optional<HoverClientCapabilities> hover;

    /// Capabilities specific to the `textDocument/signatureHelp` request.
    std::optional<SignatureHelpClientCapabilities> signatureHelp;

    /// Capabilities specific to the `textDocument/declaration` request.
    ///
    /// @since 3.14.0
    std::optional<DeclarationClientCapabilities> declaration;

    /// Capabilities specific to the `textDocument/definition` request.
    std::optional<DefinitionClientCapabilities> definition;

    /// Capabilities specific to the `textDocument/typeDefinition` request.
    ///
    /// @since 3.6.0
    std::optional<TypeDefinitionClientCapabilities> typeDefinition;

    /// Capabilities specific to the `textDocument/implementation` request.
    ///
    /// @since 3.6.0
    std::optional<ImplementationClientCapabilities> implementation;

    /// Capabilities specific to the `textDocument/references` request.
    std::optional<ReferenceClientCapabilities> references;

    /// Capabilities specific to the `textDocument/documentHighlight` request.
    std::optional<DocumentHighlightClientCapabilities> documentHighlight;

    /// Capabilities specific to the `textDocument/documentSymbol` request.
    std::optional<DocumentSymbolClientCapabilities> documentSymbol;

    /// Capabilities specific to the `textDocument/codeAction` request.
    std::optional<CodeActionClientCapabilities> codeAction;

    /// Capabilities specific to the `textDocument/codeLens` request.
    std::optional<CodeLensClientCapabilities> codeLens;

    /// Capabilities specific to the `textDocument/documentLink` request.
    std::optional<DocumentLinkClientCapabilities> documentLink;

    /// Capabilities specific to the `textDocument/documentColor` and the
    /// `textDocument/colorPresentation` request.
    ///
    /// @since 3.6.0
    std::optional<DocumentColorClientCapabilities> colorProvider;

    /// Capabilities specific to the `textDocument/formatting` request.
    std::optional<DocumentFormattingClientCapabilities> formatting;

    /// Capabilities specific to the `textDocument/rangeFormatting` request.
    std::optional<DocumentRangeFormattingClientCapabilities> rangeFormatting;

    /// Capabilities specific to the `textDocument/onTypeFormatting` request.
    std::optional<DocumentOnTypeFormattingClientCapabilities> onTypeFormatting;

    /// Capabilities specific to the `textDocument/rename` request.
    std::optional<RenameClientCapabilities> rename;

    /// Capabilities specific to the `textDocument/foldingRange` request.
    ///
    /// @since 3.10.0
    std::optional<FoldingRangeClientCapabilities> foldingRange;

    /// Capabilities specific to the `textDocument/selectionRange` request.
    ///
    /// @since 3.15.0
    std::optional<SelectionRangeClientCapabilities> selectionRange;

    /// Capabilities specific to the `textDocument/publishDiagnostics` notification.
    std::optional<PublishDiagnosticsClientCapabilities> publishDiagnostics;

    /// Capabilities specific to the various call hierarchy requests.
    ///
    /// @since 3.16.0
    std::optional<CallHierarchyClientCapabilities> callHierarchy;

    /// Capabilities specific to the various semantic token request.
    ///
    /// @since 3.16.0
    std::optional<SemanticTokensClientCapabilities> semanticTokens;

    /// Capabilities specific to the `textDocument/linkedEditingRange` request.
    ///
    /// @since 3.16.0
    std::optional<LinkedEditingRangeClientCapabilities> linkedEditingRange;

    /// Client capabilities specific to the `textDocument/moniker` request.
    ///
    /// @since 3.16.0
    std::optional<MonikerClientCapabilities> moniker;

    /// Capabilities specific to the various type hierarchy requests.
    ///
    /// @since 3.17.0
    std::optional<TypeHierarchyClientCapabilities> typeHierarchy;

    /// Capabilities specific to the `textDocument/inlineValue` request.
    ///
    /// @since 3.17.0
    std::optional<InlineValueClientCapabilities> inlineValue;

    /// Capabilities specific to the `textDocument/inlayHint` request.
    ///
    /// @since 3.17.0
    std::optional<InlayHintClientCapabilities> inlayHint;

    /// Capabilities specific to the diagnostic pull model.
    ///
    /// @since 3.17.0
    std::optional<DiagnosticClientCapabilities> diagnostic;

    /// Client capabilities specific to inline completions.
    ///
    /// @since 3.18.0
    /// @proposed
    std::optional<InlineCompletionClientCapabilities> inlineCompletion;
};

/// An interactive text edit.
///
/// @since 3.18.0
/// @proposed
struct SnippetTextEdit {
    /// The range of the text document to be manipulated.
    Range range;

    /// The snippet to be inserted.
    StringValue snippet;

    /// The actual identifier of the snippet edit.
    std::optional<ChangeAnnotationIdentifier> annotationId;
};

/// Represents the signature of something callable. A signature
/// can have a label, like a function-name, a doc-comment, and
/// a set of parameters.
struct SignatureInformation {
    /// The label of this signature. Will be shown in
    /// the UI.
    std::string label;

    /// The human-readable doc-comment of this signature. Will be shown
    /// in the UI but can be omitted.
    std::optional<rfl::Variant<std::string, MarkupContent>> documentation;

    /// The parameters of this signature.
    std::optional<std::vector<ParameterInformation>> parameters;

    /// The index of the active parameter.
    ///
    /// If `null`, no parameter of the signature is active (for example a named
    /// argument that does not match any declared parameters). This is only valid
    /// if the client specifies the client capability
    /// `textDocument.signatureHelp.noActiveParameterSupport === true`
    ///
    /// If provided (or `null`), this is used in place of
    /// `SignatureHelp.activeParameter`.
    ///
    /// @since 3.16.0
    std::optional<uint> activeParameter;
};

/// @since 3.18.0
struct ServerCompletionItemOptions {
    /// The server has support for completion item label
    /// details (see also `CompletionItemLabelDetails`) when
    /// receiving a completion item in a resolve call.
    ///
    /// @since 3.17.0
    std::optional<bool> labelDetailsSupport;
};

/// @since 3.16.0
struct SemanticTokensOptions {
    /// The legend used by the server
    SemanticTokensLegend legend;

    /// Server supports providing semantic tokens for a specific range
    /// of a document.
    std::optional<bool> range;

    /// Server supports providing semantic tokens for a full document.
    std::optional<rfl::Variant<bool, SemanticTokensFullDelta>> full;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

struct SelectionRangeOptions {
    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Save options.
struct SaveOptions {
    /// The client is supposed to include the content on save.
    std::optional<bool> includeText;
};

/// A generic resource operation.
struct ResourceOperation {
    /// The resource operation kind.
    std::string kind;

    /// An optional annotation identifier describing the operation.
    ///
    /// @since 3.16.0
    std::optional<ChangeAnnotationIdentifier> annotationId;
};

/// Rename file options
struct RenameFileOptions {
    /// Overwrite target if existing. Overwrite wins over `ignoreIfExists`
    std::optional<bool> overwrite;

    /// Ignores if target exists.
    std::optional<bool> ignoreIfExists;
};

using ProgressToken = rfl::Variant<int, std::string>;

/// A text document identifier to optionally denote a specific version of a text document.
struct OptionalVersionedTextDocumentIdentifier {
    /// The version number of this document. If a versioned text document identifier
    /// is sent from the server to the client and the file is not open in the editor
    /// (the server has not received an open notification before) the server can send
    /// `null` to indicate that the version is unknown and the content on disk is the
    /// truth (as specified with document content ownership).
    std::optional<int> version;

    /// @inherited from TextDocumentIdentifier
    /// The text document's uri.
    URI uri;
};

/// Options specific to a notebook plus its cells
/// to be synced to the server.
///
/// If a selector provides a notebook document
/// filter but no cell selector all cells of a
/// matching notebook document will be synced.
///
/// If a selector provides no notebook document
/// filter but only a cell selector all notebook
/// document that contain at least one matching
/// cell will be synced.
///
/// @since 3.17.0
struct NotebookDocumentSyncOptions {
    /// The notebooks to be synced
    std::vector<rfl::Variant<NotebookDocumentFilterWithNotebook, NotebookDocumentFilterWithCells>>
        notebookSelector;

    /// Whether save notification should be forwarded to
    /// the server. Will only be honored if mode === `notebook`.
    std::optional<bool> save;
};

/// Capabilities specific to the notebook document support.
///
/// @since 3.17.0
struct NotebookDocumentClientCapabilities {
    /// Capabilities specific to notebook document synchronization
    ///
    /// @since 3.17.0
    NotebookDocumentSyncClientCapabilities synchronization;
};

/// Content changes to a cell in a notebook document.
///
/// @since 3.18.0
struct NotebookDocumentCellContentChanges {
    VersionedTextDocumentIdentifier document;

    std::vector<TextDocumentContentChangeEvent> changes;
};

/// Structural changes to cells in a notebook document.
///
/// @since 3.18.0
struct NotebookDocumentCellChangeStructure {
    /// The change to the cell array.
    NotebookCellArrayChange array;

    /// Additional opened cell text documents.
    std::optional<std::vector<TextDocumentItem>> didOpen;

    /// Additional closed cell text documents.
    std::optional<std::vector<TextDocumentIdentifier>> didClose;
};

struct MonikerOptions {
    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

struct LinkedEditingRangeOptions {
    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Inline value options used during static registration.
///
/// @since 3.17.0
struct InlineValueOptions {
    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Inlay hint options used during static registration.
///
/// @since 3.17.0
struct InlayHintOptions {
    /// The server provides support to resolve additional
    /// information for an inlay hint item.
    std::optional<bool> resolveProvider;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

struct ImplementationOptions {
    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// General client capabilities.
///
/// @since 3.16.0
struct GeneralClientCapabilities {
    /// Client capability that signals how the client
    /// handles stale requests (e.g. a request
    /// for which the client will not process the response
    /// anymore since the information is outdated).
    ///
    /// @since 3.17.0
    std::optional<StaleRequestSupportOptions> staleRequestSupport;

    /// Client capabilities specific to regular expressions.
    ///
    /// @since 3.16.0
    std::optional<RegularExpressionsClientCapabilities> regularExpressions;

    /// Client capabilities specific to the client's markdown parser.
    ///
    /// @since 3.16.0
    std::optional<MarkdownClientCapabilities> markdown;

    /// The position encodings supported by the client. Client and server
    /// have to agree on the same position encoding to ensure that offsets
    /// (e.g. character position in a line) are interpreted the same on both
    /// sides.
    ///
    /// To keep the protocol backwards compatible the following applies: if
    /// the value 'utf-16' is missing from the array of position encodings
    /// servers can assume that the client supports UTF-16. UTF-16 is
    /// therefore a mandatory encoding.
    ///
    /// If omitted it defaults to ['utf-16'].
    ///
    /// Implementation considerations: since the conversion from one encoding
    /// into another requires the content of the file / line the conversion
    /// is best done where the file is read which is usually on the server
    /// side.
    ///
    /// @since 3.17.0
    std::optional<std::vector<PositionEncodingKind>> positionEncodings;
};

/// A diagnostic report with a full set of problems.
///
/// @since 3.17.0
struct FullDocumentDiagnosticReport {
    /// A full document diagnostic report.
    rfl::Literal<"full"> kind;

    /// An optional result id. If provided it will
    /// be sent on the next diagnostic request for the
    /// same document.
    std::optional<std::string> resultId;

    /// The actual items.
    std::vector<Diagnostic> items;
};

struct FoldingRangeOptions {
    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Options for notifications/requests for user operations on files.
///
/// @since 3.16.0
struct FileOperationOptions {
    /// The server is interested in receiving didCreateFiles notifications.
    std::optional<FileOperationRegistrationOptions> didCreate;

    /// The server is interested in receiving willCreateFiles requests.
    std::optional<FileOperationRegistrationOptions> willCreate;

    /// The server is interested in receiving didRenameFiles notifications.
    std::optional<FileOperationRegistrationOptions> didRename;

    /// The server is interested in receiving willRenameFiles requests.
    std::optional<FileOperationRegistrationOptions> willRename;

    /// The server is interested in receiving didDeleteFiles file notifications.
    std::optional<FileOperationRegistrationOptions> didDelete;

    /// The server is interested in receiving willDeleteFiles file requests.
    std::optional<FileOperationRegistrationOptions> willDelete;
};

struct DocumentColorOptions {
    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Diagnostic options.
///
/// @since 3.17.0
struct DiagnosticOptions {
    /// An optional identifier under which the diagnostics are
    /// managed by the client.
    std::optional<std::string> identifier;

    /// Whether the language has inter file dependencies meaning that
    /// editing code in one file can result in a different diagnostic
    /// set in another file. Inter file dependencies are common for
    /// most programming languages and typically uncommon for linters.
    bool interFileDependencies;

    /// The server provides support for workspace diagnostics as well.
    bool workspaceDiagnostics;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Delete file options
struct DeleteFileOptions {
    /// Delete the content recursively if a folder is denoted.
    std::optional<bool> recursive;

    /// Ignore the operation if the file doesn't exist.
    std::optional<bool> ignoreIfNotExists;
};

struct DeclarationOptions {
    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Options to create a file.
struct CreateFileOptions {
    /// Overwrite existing file. Overwrite wins over `ignoreIfExists`
    std::optional<bool> overwrite;

    /// Ignore if exists.
    std::optional<bool> ignoreIfExists;
};

/// Documentation for a class of code actions.
///
/// @since 3.18.0
/// @proposed
struct CodeActionKindDocumentation {
    /// The kind of the code action being documented.
    ///
    /// If the kind is generic, such as `CodeActionKind.Refactor`, the documentation will be shown
    /// whenever any refactorings are returned. If the kind if more specific, such as
    /// `CodeActionKind.RefactorExtract`, the documentation will only be shown when extract
    /// refactoring code actions are returned.
    CodeActionKind kind;

    /// Command that is ued to display the documentation to the user.
    ///
    /// The title of this documentation code action is taken from {@linkcode Command.title}
    Command command;
};

/// Call hierarchy options used during static registration.
///
/// @since 3.16.0
struct CallHierarchyOptions {
    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// A special text edit with an additional change annotation.
///
/// @since 3.16.0.
struct AnnotatedTextEdit {
    /// The actual identifier of the change annotation
    ChangeAnnotationIdentifier annotationId;

    /// @inherited from TextEdit
    /// The range of the text document to be manipulated. To insert
    /// text into a document create a range where start === end.
    Range range;

    /// @inherited from TextEdit
    /// The string to be inserted. For delete operations use an
    /// empty string.
    std::string newText;
};

/// An unchanged document diagnostic report for a workspace diagnostic result.
///
/// @since 3.17.0
struct WorkspaceUnchangedDocumentDiagnosticReport {
    /// The URI for which diagnostic information is reported.
    URI uri;

    /// The version number for which the diagnostics are reported.
    /// If the document is not marked as open `null` can be provided.
    std::optional<int> version;

    /// @inherited from UnchangedDocumentDiagnosticReport
    /// A document diagnostic report indicating
    /// no changes to the last result. A server can
    /// only return `unchanged` if result ids are
    /// provided.
    rfl::Literal<"unchanged"> kind;

    /// @inherited from UnchangedDocumentDiagnosticReport
    /// A result id which will be sent on the next
    /// diagnostic request for the same document.
    std::string resultId;
};

/// Server capabilities for a {@link WorkspaceSymbolRequest}.
struct WorkspaceSymbolOptions {
    /// The server provides support to resolve additional
    /// information for a workspace symbol.
    ///
    /// @since 3.17.0
    std::optional<bool> resolveProvider;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Defines workspace specific capabilities of the server.
///
/// @since 3.18.0
struct WorkspaceOptions {
    /// The server supports workspace folder.
    ///
    /// @since 3.6.0
    std::optional<WorkspaceFoldersServerCapabilities> workspaceFolders;

    /// The server is interested in notifications/requests for operations on files.
    ///
    /// @since 3.16.0
    std::optional<FileOperationOptions> fileOperations;

    /// The server supports the `workspace/textDocumentContent` request.
    ///
    /// @since 3.18.0
    /// @proposed
    std::optional<rfl::Variant<TextDocumentContentOptions, TextDocumentContentRegistrationOptions>>
        textDocumentContent;
};

/// A full document diagnostic report for a workspace diagnostic result.
///
/// @since 3.17.0
struct WorkspaceFullDocumentDiagnosticReport {
    /// The URI for which diagnostic information is reported.
    URI uri;

    /// The version number for which the diagnostics are reported.
    /// If the document is not marked as open `null` can be provided.
    std::optional<int> version;

    /// @inherited from FullDocumentDiagnosticReport
    /// A full document diagnostic report.
    rfl::Literal<"full"> kind;

    /// @inherited from FullDocumentDiagnosticReport
    /// An optional result id. If provided it will
    /// be sent on the next diagnostic request for the
    /// same document.
    std::optional<std::string> resultId;

    /// @inherited from FullDocumentDiagnosticReport
    /// The actual items.
    std::vector<Diagnostic> items;
};

struct WorkDoneProgressParams {
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;
};

/// Type hierarchy options used during static or dynamic registration.
///
/// @since 3.17.0
struct TypeHierarchyRegistrationOptions {
    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;

    /// @mixin from StaticRegistrationOptions
    /// The id used to register the request. The id can be used to deregister
    /// the request again. See also Registration#id.
    std::optional<std::string> id;
};

struct TypeDefinitionRegistrationOptions {
    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;

    /// @mixin from StaticRegistrationOptions
    /// The id used to register the request. The id can be used to deregister
    /// the request again. See also Registration#id.
    std::optional<std::string> id;
};

using TraceValue = rfl::Literal<"off", "messages", "verbose">;

struct TextDocumentSyncOptions {
    /// Open and close notifications are sent to the server. If omitted open close notification
    /// should not be sent.
    std::optional<bool> openClose;

    /// Change notifications are sent to the server. See TextDocumentSyncKind.None,
    /// TextDocumentSyncKind.Full and TextDocumentSyncKind.Incremental. If omitted it defaults to
    /// TextDocumentSyncKind.None.
    std::optional<TextDocumentSyncKind> change;

    /// If present will save notifications are sent to the server. If omitted the notification
    /// should not be sent.
    std::optional<bool> willSave;

    /// If present will save wait until requests are sent to the server. If omitted the request
    /// should not be sent.
    std::optional<bool> willSaveWaitUntil;

    /// If present save notifications are sent to the server. If omitted the notification should not
    /// be sent.
    std::optional<rfl::Variant<bool, SaveOptions>> save;
};

/// Describes textual changes on a text document. A TextDocumentEdit describes all changes
/// on a document version Si and after they are applied move the document to version Si+1.
/// So the creator of a TextDocumentEdit doesn't need to sort the array of edits or do any
/// kind of ordering. However the edits must be non overlapping.
struct TextDocumentEdit {
    /// The text document to change.
    OptionalVersionedTextDocumentIdentifier textDocument;

    /// The edits to be applied.
    ///
    /// @since 3.16.0 - support for AnnotatedTextEdit. This is guarded using a
    /// client capability.
    ///
    /// @since 3.18.0 - support for SnippetTextEdit. This is guarded using a
    /// client capability.
    std::vector<rfl::Variant<TextEdit, AnnotatedTextEdit, SnippetTextEdit>> edits;
};

/// How a signature help was triggered.
///
/// @since 3.15.0
enum class SignatureHelpTriggerKind : uint8_t {
    Invoked = 1,
    TriggerCharacter = 2,
    ContentChange = 3,
};

/// Server Capabilities for a {@link SignatureHelpRequest}.
struct SignatureHelpOptions {
    /// List of characters that trigger signature help automatically.
    std::optional<std::vector<std::string>> triggerCharacters;

    /// List of characters that re-trigger signature help.
    ///
    /// These trigger characters are only active when signature help is already showing. All trigger
    /// characters are also counted as re-trigger characters.
    ///
    /// @since 3.15.0
    std::optional<std::vector<std::string>> retriggerCharacters;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Signature help represents the signature of something
/// callable. There can be multiple signature but only one
/// active and only one active parameter.
struct SignatureHelp {
    /// One or more signatures.
    std::vector<SignatureInformation> signatures;

    /// The active signature. If omitted or the value lies outside the
    /// range of `signatures` the value defaults to zero or is ignored if
    /// the `SignatureHelp` has no signatures.
    ///
    /// Whenever possible implementors should make an active decision about
    /// the active signature and shouldn't rely on a default value.
    ///
    /// In future version of the protocol this property might become
    /// mandatory to better express this.
    std::optional<uint> activeSignature;

    /// The active parameter of the active signature.
    ///
    /// If `null`, no parameter of the signature is active (for example a named
    /// argument that does not match any declared parameters). This is only valid
    /// if the client specifies the client capability
    /// `textDocument.signatureHelp.noActiveParameterSupport === true`
    ///
    /// If omitted or the value lies outside the range of
    /// `signatures[activeSignature].parameters` defaults to 0 if the active
    /// signature has parameters.
    ///
    /// If the active signature has no parameters it is ignored.
    ///
    /// In future version of the protocol this property might become
    /// mandatory (but still nullable) to better express the active parameter if
    /// the active signature does have any.
    std::optional<uint> activeParameter;
};

/// @since 3.16.0
struct SemanticTokensRegistrationOptions {
    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @inherited from SemanticTokensOptions
    /// The legend used by the server
    SemanticTokensLegend legend;

    /// @inherited from SemanticTokensOptions
    /// Server supports providing semantic tokens for a specific range
    /// of a document.
    std::optional<bool> range;

    /// @inherited from SemanticTokensOptions
    /// Server supports providing semantic tokens for a full document.
    std::optional<rfl::Variant<bool, SemanticTokensFullDelta>> full;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;

    /// @mixin from StaticRegistrationOptions
    /// The id used to register the request. The id can be used to deregister
    /// the request again. See also Registration#id.
    std::optional<std::string> id;
};

struct SelectionRangeRegistrationOptions {
    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;

    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @mixin from StaticRegistrationOptions
    /// The id used to register the request. The id can be used to deregister
    /// the request again. See also Registration#id.
    std::optional<std::string> id;
};

/// Describes the currently selected completion item.
///
/// @since 3.18.0
/// @proposed
struct SelectedCompletionInfo {
    /// The range that will be replaced if this completion item is accepted.
    Range range;

    /// The text the range will be replaced with if this completion is accepted.
    std::string text;
};

/// Provider options for a {@link RenameRequest}.
struct RenameOptions {
    /// Renames should be checked and tested before being executed.
    ///
    /// @since version 3.12.0
    std::optional<bool> prepareProvider;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Rename file operation
struct RenameFile {
    /// A rename
    rfl::Literal<"rename"> kind;

    /// The old (existing) location.
    URI oldUri;

    /// The new location.
    URI newUri;

    /// Rename options.
    std::optional<RenameFileOptions> options;

    /// skipping kind @inherited from ResourceOperation
    /// @inherited from ResourceOperation
    /// An optional annotation identifier describing the operation.
    ///
    /// @since 3.16.0
    std::optional<ChangeAnnotationIdentifier> annotationId;
};

/// Reference options.
struct ReferenceOptions {
    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Registration options specific to a notebook.
///
/// @since 3.17.0
struct NotebookDocumentSyncRegistrationOptions {
    /// @inherited from NotebookDocumentSyncOptions
    /// The notebooks to be synced
    std::vector<rfl::Variant<NotebookDocumentFilterWithNotebook, NotebookDocumentFilterWithCells>>
        notebookSelector;

    /// @inherited from NotebookDocumentSyncOptions
    /// Whether save notification should be forwarded to
    /// the server. Will only be honored if mode === `notebook`.
    std::optional<bool> save;

    /// @mixin from StaticRegistrationOptions
    /// The id used to register the request. The id can be used to deregister
    /// the request again. See also Registration#id.
    std::optional<std::string> id;
};

/// Cell changes to a notebook document.
///
/// @since 3.18.0
struct NotebookDocumentCellChanges {
    /// Changes to the cell structure to add or
    /// remove cells.
    std::optional<NotebookDocumentCellChangeStructure> structure;

    /// Changes to notebook cells properties like its
    /// kind, execution summary or metadata.
    std::optional<std::vector<NotebookCell>> data;

    /// Changes to the text content of notebook cells.
    std::optional<std::vector<NotebookDocumentCellContentChanges>> textContent;
};

struct MonikerRegistrationOptions {
    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// @since 3.18.0
/// @deprecated use MarkupContent instead.
struct MarkedStringWithLanguage {
    std::string language;

    std::string value;
};

struct LinkedEditingRangeRegistrationOptions {
    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;

    /// @mixin from StaticRegistrationOptions
    /// The id used to register the request. The id can be used to deregister
    /// the request again. See also Registration#id.
    std::optional<std::string> id;
};

/// Defines whether the insert text in a completion item should be interpreted as
/// plain text or a snippet.
enum class InsertTextFormat : uint8_t {
    PlainText = 1,
    Snippet = 2,
};

/// A special text edit to provide an insert and a replace operation.
///
/// @since 3.16.0
struct InsertReplaceEdit {
    /// The string to be inserted.
    std::string newText;

    /// The range if the insert is requested
    Range insert;

    /// The range if the replace is requested.
    Range replace;
};

/// Inline value options used during static or dynamic registration.
///
/// @since 3.17.0
struct InlineValueRegistrationOptions {
    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;

    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @mixin from StaticRegistrationOptions
    /// The id used to register the request. The id can be used to deregister
    /// the request again. See also Registration#id.
    std::optional<std::string> id;
};

/// Describes how an {@link InlineCompletionItemProvider inline completion provider} was triggered.
///
/// @since 3.18.0
/// @proposed
enum class InlineCompletionTriggerKind : uint8_t {
    Invoked = 1,
    Automatic = 2,
};

/// Inline completion options used during static registration.
///
/// @since 3.18.0
/// @proposed
struct InlineCompletionOptions {
    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Inlay hint options used during static or dynamic registration.
///
/// @since 3.17.0
struct InlayHintRegistrationOptions {
    /// @inherited from InlayHintOptions
    /// The server provides support to resolve additional
    /// information for an inlay hint item.
    std::optional<bool> resolveProvider;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;

    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @mixin from StaticRegistrationOptions
    /// The id used to register the request. The id can be used to deregister
    /// the request again. See also Registration#id.
    std::optional<std::string> id;
};

struct ImplementationRegistrationOptions {
    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;

    /// @mixin from StaticRegistrationOptions
    /// The id used to register the request. The id can be used to deregister
    /// the request again. See also Registration#id.
    std::optional<std::string> id;
};

/// Hover options.
struct HoverOptions {
    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

struct FoldingRangeRegistrationOptions {
    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;

    /// @mixin from StaticRegistrationOptions
    /// The id used to register the request. The id can be used to deregister
    /// the request again. See also Registration#id.
    std::optional<std::string> id;
};

/// The file event type
enum class FileChangeType : uint8_t {
    Created = 1,
    Changed = 2,
    Deleted = 3,
};

/// The server capabilities of a {@link ExecuteCommandRequest}.
struct ExecuteCommandOptions {
    /// The commands to be executed on the server
    std::vector<std::string> commands;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Edit range variant that includes ranges for insert and replace operations.
///
/// @since 3.18.0
struct EditRangeWithInsertReplace {
    Range insert;

    Range replace;
};

/// Provider options for a {@link DocumentSymbolRequest}.
struct DocumentSymbolOptions {
    /// A human-readable string that is shown when multiple outlines trees
    /// are shown for the same document.
    ///
    /// @since 3.16.0
    std::optional<std::string> label;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Provider options for a {@link DocumentRangeFormattingRequest}.
struct DocumentRangeFormattingOptions {
    /// Whether the server supports formatting multiple ranges at once.
    ///
    /// @since 3.18.0
    /// @proposed
    std::optional<bool> rangesSupport;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Provider options for a {@link DocumentOnTypeFormattingRequest}.
struct DocumentOnTypeFormattingOptions {
    /// A character on which formatting should be triggered, like `{`.
    std::string firstTriggerCharacter;

    /// More trigger characters.
    std::optional<std::vector<std::string>> moreTriggerCharacter;
};

/// Provider options for a {@link DocumentLinkRequest}.
struct DocumentLinkOptions {
    /// Document links have a resolve provider as well.
    std::optional<bool> resolveProvider;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Provider options for a {@link DocumentHighlightRequest}.
struct DocumentHighlightOptions {
    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Provider options for a {@link DocumentFormattingRequest}.
struct DocumentFormattingOptions {
    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

struct DocumentColorRegistrationOptions {
    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;

    /// @mixin from StaticRegistrationOptions
    /// The id used to register the request. The id can be used to deregister
    /// the request again. See also Registration#id.
    std::optional<std::string> id;
};

/// Diagnostic registration options.
///
/// @since 3.17.0
struct DiagnosticRegistrationOptions {
    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @inherited from DiagnosticOptions
    /// An optional identifier under which the diagnostics are
    /// managed by the client.
    std::optional<std::string> identifier;

    /// @inherited from DiagnosticOptions
    /// Whether the language has inter file dependencies meaning that
    /// editing code in one file can result in a different diagnostic
    /// set in another file. Inter file dependencies are common for
    /// most programming languages and typically uncommon for linters.
    bool interFileDependencies;

    /// @inherited from DiagnosticOptions
    /// The server provides support for workspace diagnostics as well.
    bool workspaceDiagnostics;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;

    /// @mixin from StaticRegistrationOptions
    /// The id used to register the request. The id can be used to deregister
    /// the request again. See also Registration#id.
    std::optional<std::string> id;
};

/// Delete file operation
struct DeleteFile {
    /// A delete
    rfl::Literal<"delete"> kind;

    /// The file to delete.
    URI uri;

    /// Delete options.
    std::optional<DeleteFileOptions> options;

    /// skipping kind @inherited from ResourceOperation
    /// @inherited from ResourceOperation
    /// An optional annotation identifier describing the operation.
    ///
    /// @since 3.16.0
    std::optional<ChangeAnnotationIdentifier> annotationId;
};

/// Server Capabilities for a {@link DefinitionRequest}.
struct DefinitionOptions {
    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

struct DeclarationRegistrationOptions {
    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;

    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @mixin from StaticRegistrationOptions
    /// The id used to register the request. The id can be used to deregister
    /// the request again. See also Registration#id.
    std::optional<std::string> id;
};

/// Create file operation.
struct CreateFile {
    /// A create
    rfl::Literal<"create"> kind;

    /// The resource to create.
    URI uri;

    /// Additional options
    std::optional<CreateFileOptions> options;

    /// skipping kind @inherited from ResourceOperation
    /// @inherited from ResourceOperation
    /// An optional annotation identifier describing the operation.
    ///
    /// @since 3.16.0
    std::optional<ChangeAnnotationIdentifier> annotationId;
};

/// How a completion was triggered
enum class CompletionTriggerKind : uint8_t {
    Invoked = 1,
    TriggerCharacter = 2,
    TriggerForIncompleteCompletions = 3,
};

/// Completion options.
struct CompletionOptions {
    /// Most tools trigger completion request automatically without explicitly requesting
    /// it using a keyboard shortcut (e.g. Ctrl+Space). Typically they do so when the user
    /// starts to type an identifier. For example if the user types `c` in a JavaScript file
    /// code complete will automatically pop up present `console` besides others as a
    /// completion item. Characters that make up identifiers don't need to be listed here.
    ///
    /// If code complete should automatically be trigger on characters not being valid inside
    /// an identifier (for example `.` in JavaScript) list them in `triggerCharacters`.
    std::optional<std::vector<std::string>> triggerCharacters;

    /// The list of all possible characters that commit a completion. This field can be used
    /// if clients don't support individual commit characters per completion item. See
    /// `ClientCapabilities.textDocument.completion.completionItem.commitCharactersSupport`
    ///
    /// If a server provides both `allCommitCharacters` and commit characters on an individual
    /// completion item the ones on the completion item win.
    ///
    /// @since 3.2.0
    std::optional<std::vector<std::string>> allCommitCharacters;

    /// The server provides support to resolve additional
    /// information for a completion item.
    std::optional<bool> resolveProvider;

    /// The server supports the following `CompletionItem` specific
    /// capabilities.
    ///
    /// @since 3.17.0
    std::optional<ServerCompletionItemOptions> completionItem;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Additional details for a completion item label.
///
/// @since 3.17.0
struct CompletionItemLabelDetails {
    /// An optional string which is rendered less prominently directly after {@link
    /// CompletionItem.label label}, without any spacing. Should be used for function signatures and
    /// type annotations.
    std::optional<std::string> detail;

    /// An optional string which is rendered less prominently after {@link CompletionItem.detail}.
    /// Should be used for fully qualified names and file paths.
    std::optional<std::string> description;
};

/// Code Lens provider options of a {@link CodeLensRequest}.
struct CodeLensOptions {
    /// Code lens has a resolve provider as well.
    std::optional<bool> resolveProvider;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// The reason why code actions were requested.
///
/// @since 3.17.0
enum class CodeActionTriggerKind : uint8_t {
    Invoked = 1,
    Automatic = 2,
};

/// Provider options for a {@link CodeActionRequest}.
struct CodeActionOptions {
    /// CodeActionKinds that this server may return.
    ///
    /// The list of kinds may be generic, such as `CodeActionKind.Refactor`, or the server
    /// may list out every specific kind they provide.
    std::optional<std::vector<CodeActionKind>> codeActionKinds;

    /// Static documentation for a class of code actions.
    ///
    /// Documentation from the provider should be shown in the code actions menu if either:
    ///
    /// - Code actions of `kind` are requested by the editor. In this case, the editor will show the
    /// documentation that
    ///   most closely matches the requested code action kind. For example, if a provider has
    ///   documentation for both `Refactor` and `RefactorExtract`, when the user requests code
    ///   actions for `RefactorExtract`, the editor will use the documentation for `RefactorExtract`
    ///   instead of the documentation for `Refactor`.
    ///
    /// - Any code actions of `kind` are returned by the provider.
    ///
    /// At most one documentation entry should be shown per provider.
    ///
    /// @since 3.18.0
    /// @proposed
    std::optional<std::vector<CodeActionKindDocumentation>> documentation;

    /// The server provides support to resolve additional
    /// information for a code action.
    ///
    /// @since 3.16.0
    std::optional<bool> resolveProvider;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Information about the client
///
/// @since 3.15.0
/// @since 3.18.0 ClientInfo type name added.
struct ClientInfo {
    /// The name of the client as defined by the client.
    std::string name;

    /// The client's version as defined by the client.
    std::optional<std::string> version;
};

/// Defines the capabilities provided by the client.
struct ClientCapabilities {
    /// Workspace specific client capabilities.
    std::optional<WorkspaceClientCapabilities> workspace;

    /// Text document specific client capabilities.
    std::optional<TextDocumentClientCapabilities> textDocument;

    /// Capabilities specific to the notebook document support.
    ///
    /// @since 3.17.0
    std::optional<NotebookDocumentClientCapabilities> notebookDocument;

    /// Window specific client capabilities.
    std::optional<WindowClientCapabilities> window;

    /// General client capabilities.
    ///
    /// @since 3.16.0
    std::optional<GeneralClientCapabilities> general;

    /// Experimental client capabilities.
    std::optional<LSPAny> experimental;
};

/// Additional information that describes document changes.
///
/// @since 3.16.0
struct ChangeAnnotation {
    /// A human-readable string describing the actual change. The string
    /// is rendered prominent in the user interface.
    std::string label;

    /// A flag which indicates that user confirmation is needed
    /// before applying the change.
    std::optional<bool> needsConfirmation;

    /// A human-readable string which is rendered less prominent in
    /// the user interface.
    std::optional<std::string> description;
};

/// Call hierarchy options used during static or dynamic registration.
///
/// @since 3.16.0
struct CallHierarchyRegistrationOptions {
    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;

    /// @mixin from StaticRegistrationOptions
    /// The id used to register the request. The id can be used to deregister
    /// the request again. See also Registration#id.
    std::optional<std::string> id;
};

/// Defines how values from a set of defaults and an individual item will be
/// merged.
///
/// @since 3.18.0
enum class ApplyKind : uint8_t {
    Replace = 1,
    Merge = 2,
};

/// The initialize parameters
struct _InitializeParams {
    /// The process Id of the parent process that started
    /// the server.
    ///
    /// Is `null` if the process has not been started by another process.
    /// If the parent process is not alive then the server should exit.
    std::optional<int> processId;

    /// Information about the client
    ///
    /// @since 3.15.0
    std::optional<ClientInfo> clientInfo;

    /// The locale the client is currently showing the user interface
    /// in. This must not necessarily be the locale of the operating
    /// system.
    ///
    /// Uses IETF language tags as the value's syntax
    /// (See https://en.wikipedia.org/wiki/IETF_language_tag)
    ///
    /// @since 3.16.0
    std::optional<std::string> locale;

    /// The rootPath of the workspace. Is null
    /// if no folder is open.
    ///
    /// @deprecated in favour of rootUri.
    std::optional<std::string> rootPath;

    /// The rootUri of the workspace. Is null if no
    /// folder is open. If both `rootPath` and `rootUri` are set
    /// `rootUri` wins.
    ///
    /// @deprecated in favour of workspaceFolders.
    std::optional<URI> rootUri;

    /// The capabilities provided by the client (editor or tool)
    ClientCapabilities capabilities;

    /// User provided initialization options.
    std::optional<LSPAny> initializationOptions;

    /// The initial trace setting. If omitted trace is disabled ('off').
    std::optional<TraceValue> trace;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;
};

struct WorkspaceFoldersInitializeParams {
    /// The workspace folders configured in the client when the server starts.
    ///
    /// This property is only available if the client supports workspace folders.
    /// It can be `null` if the client supports workspace folders but none are
    /// configured.
    ///
    /// @since 3.6.0
    std::optional<std::vector<WorkspaceFolder>> workspaceFolders;
};

/// The workspace folder change event.
struct WorkspaceFoldersChangeEvent {
    /// The array of added workspace folders
    std::vector<WorkspaceFolder> added;

    /// The array of the removed workspace folders
    std::vector<WorkspaceFolder> removed;
};

/// Additional data about a workspace edit.
///
/// @since 3.18.0
/// @proposed
struct WorkspaceEditMetadata {
    /// Signal to the editor that this edit is a refactoring.
    std::optional<bool> isRefactoring;
};

/// A workspace edit represents changes to many resources managed in the workspace. The edit
/// should either provide `changes` or `documentChanges`. If documentChanges are present
/// they are preferred over `changes` if the client can handle versioned document edits.
///
/// Since version 3.13.0 a workspace edit can contain resource operations as well. If resource
/// operations are present clients need to execute the operations in the order in which they
/// are provided. So a workspace edit for example can consist of the following two changes:
/// (1) a create file a.txt and (2) a text document edit which insert text into file a.txt.
///
/// An invalid sequence (e.g. (1) delete file a.txt and (2) insert text into file a.txt) will
/// cause failure of the operation. How the client recovers from the failure is described by
/// the client capability: `workspace.workspaceEdit.failureHandling`
struct WorkspaceEdit {
    /// Holds changes to existing resources.
    std::optional<std::unordered_map<std::string, std::vector<TextEdit>>> changes;

    /// Depending on the client capability `workspace.workspaceEdit.resourceOperations` document
    /// changes are either an array of `TextDocumentEdit`s to express changes to n different text
    /// documents where each text document edit addresses a specific version of a text document. Or
    /// it can contain above `TextDocumentEdit`s mixed with create, rename and delete file / folder
    /// operations.
    ///
    /// Whether a client supports versioned document edits is expressed via
    /// `workspace.workspaceEdit.documentChanges` client capability.
    ///
    /// If a client neither supports `documentChanges` nor
    /// `workspace.workspaceEdit.resourceOperations` then only plain `TextEdit`s using the `changes`
    /// property are supported.
    std::optional<std::vector<rfl::Variant<TextDocumentEdit, CreateFile, RenameFile, DeleteFile>>>
        documentChanges;

    /// A map of change annotations that can be referenced in `AnnotatedTextEdit`s or create, rename
    /// and delete file / folder operations.
    ///
    /// Whether clients honor this property depends on the client capability
    /// `workspace.changeAnnotationSupport`.
    ///
    /// @since 3.16.0
    std::optional<std::unordered_map<std::string, ChangeAnnotation>> changeAnnotations;
};

/// A workspace diagnostic document report.
///
/// @since 3.17.0
using WorkspaceDocumentDiagnosticReport =
    rfl::Variant<WorkspaceFullDocumentDiagnosticReport, WorkspaceUnchangedDocumentDiagnosticReport>;

enum class WatchKind : uint8_t {
    Create = 1,
    Change = 2,
    Delete = 4,
};

/// A versioned notebook document identifier.
///
/// @since 3.17.0
struct VersionedNotebookDocumentIdentifier {
    /// The version number of this notebook document.
    int version;

    /// The notebook document's uri.
    URI uri;
};

/// General parameters to unregister a request or notification.
struct Unregistration {
    /// The id used to unregister the request or notification. Usually an id
    /// provided during the register request.
    std::string id;

    /// The method to unregister for.
    std::string method;
};

/// Moniker uniqueness level to define scope of the moniker.
///
/// @since 3.16.0
using UniquenessLevel = rfl::Literal<"document", "project", "group", "scheme", "global">;

/// @since 3.17.0
struct TypeHierarchyItem {
    /// The name of this item.
    std::string name;

    /// The kind of this item.
    SymbolKind kind;

    /// Tags for this item.
    std::optional<std::vector<SymbolTag>> tags;

    /// More detail for this item, e.g. the signature of a function.
    std::optional<std::string> detail;

    /// The resource identifier of this item.
    URI uri;

    /// The range enclosing this symbol not including leading/trailing whitespace
    /// but everything else, e.g. comments and code.
    Range range;

    /// The range that should be selected and revealed when this symbol is being
    /// picked, e.g. the name of a function. Must be contained by the
    /// {@link TypeHierarchyItem.range `range`}.
    Range selectionRange;

    /// A data entry field that is preserved between a type hierarchy prepare and
    /// supertypes or subtypes requests. It could also be used to identify the
    /// type hierarchy in the server, helping improve the performance on
    /// resolving supertypes and subtypes.
    std::optional<LSPAny> data;
};

/// Represents reasons why a text document is saved.
enum class TextDocumentSaveReason : uint8_t {
    Manual = 1,
    AfterDelay = 2,
    FocusOut = 3,
};

/// A parameter literal used in requests to pass a text document and a position inside that
/// document.
struct TextDocumentPositionParams {
    /// The text document.
    TextDocumentIdentifier textDocument;

    /// The position inside the text document.
    Position position;
};

/// Additional information about the context in which a signature help request was triggered.
///
/// @since 3.15.0
struct SignatureHelpContext {
    /// Action that caused signature help to be triggered.
    SignatureHelpTriggerKind triggerKind;

    /// Character that caused signature help to be triggered.
    ///
    /// This is undefined when `triggerKind !== SignatureHelpTriggerKind.TriggerCharacter`
    std::optional<std::string> triggerCharacter;

    /// `true` if signature help was already showing when it was triggered.
    ///
    /// Retriggers occurs when the signature help is already active and can be caused by actions
    /// such as typing a trigger character, a cursor move, or document content changes.
    bool isRetrigger;

    /// The currently active `SignatureHelp`.
    ///
    /// The `activeSignatureHelp` has its `SignatureHelp.activeSignature` field updated based on
    /// the user navigating through available signatures.
    std::optional<SignatureHelp> activeSignatureHelp;
};

/// Information about the server
///
/// @since 3.15.0
/// @since 3.18.0 ServerInfo type name added.
struct ServerInfo {
    /// The name of the server as defined by the server.
    std::string name;

    /// The server's version as defined by the server.
    std::optional<std::string> version;
};

/// Defines the capabilities provided by a language
/// server.
struct ServerCapabilities {
    /// The position encoding the server picked from the encodings offered
    /// by the client via the client capability `general.positionEncodings`.
    ///
    /// If the client didn't provide any position encodings the only valid
    /// value that a server can return is 'utf-16'.
    ///
    /// If omitted it defaults to 'utf-16'.
    ///
    /// @since 3.17.0
    std::optional<PositionEncodingKind> positionEncoding;

    /// Defines how text documents are synced. Is either a detailed structure
    /// defining each notification or for backwards compatibility the
    /// TextDocumentSyncKind number.
    std::optional<rfl::Variant<TextDocumentSyncOptions, TextDocumentSyncKind>> textDocumentSync;

    /// Defines how notebook documents are synced.
    ///
    /// @since 3.17.0
    std::optional<
        rfl::Variant<NotebookDocumentSyncOptions, NotebookDocumentSyncRegistrationOptions>>
        notebookDocumentSync;

    /// The server provides completion support.
    std::optional<CompletionOptions> completionProvider;

    /// The server provides hover support.
    std::optional<rfl::Variant<bool, HoverOptions>> hoverProvider;

    /// The server provides signature help support.
    std::optional<SignatureHelpOptions> signatureHelpProvider;

    /// The server provides Goto Declaration support.
    std::optional<rfl::Variant<bool, DeclarationOptions, DeclarationRegistrationOptions>>
        declarationProvider;

    /// The server provides goto definition support.
    std::optional<rfl::Variant<bool, DefinitionOptions>> definitionProvider;

    /// The server provides Goto Type Definition support.
    std::optional<rfl::Variant<bool, TypeDefinitionOptions, TypeDefinitionRegistrationOptions>>
        typeDefinitionProvider;

    /// The server provides Goto Implementation support.
    std::optional<rfl::Variant<bool, ImplementationOptions, ImplementationRegistrationOptions>>
        implementationProvider;

    /// The server provides find references support.
    std::optional<rfl::Variant<bool, ReferenceOptions>> referencesProvider;

    /// The server provides document highlight support.
    std::optional<rfl::Variant<bool, DocumentHighlightOptions>> documentHighlightProvider;

    /// The server provides document symbol support.
    std::optional<rfl::Variant<bool, DocumentSymbolOptions>> documentSymbolProvider;

    /// The server provides code actions. CodeActionOptions may only be
    /// specified if the client states that it supports
    /// `codeActionLiteralSupport` in its initial `initialize` request.
    std::optional<rfl::Variant<bool, CodeActionOptions>> codeActionProvider;

    /// The server provides code lens.
    std::optional<CodeLensOptions> codeLensProvider;

    /// The server provides document link support.
    std::optional<DocumentLinkOptions> documentLinkProvider;

    /// The server provides color provider support.
    std::optional<rfl::Variant<bool, DocumentColorOptions, DocumentColorRegistrationOptions>>
        colorProvider;

    /// The server provides workspace symbol support.
    std::optional<rfl::Variant<bool, WorkspaceSymbolOptions>> workspaceSymbolProvider;

    /// The server provides document formatting.
    std::optional<rfl::Variant<bool, DocumentFormattingOptions>> documentFormattingProvider;

    /// The server provides document range formatting.
    std::optional<rfl::Variant<bool, DocumentRangeFormattingOptions>>
        documentRangeFormattingProvider;

    /// The server provides document formatting on typing.
    std::optional<DocumentOnTypeFormattingOptions> documentOnTypeFormattingProvider;

    /// The server provides rename support. RenameOptions may only be
    /// specified if the client states that it supports
    /// `prepareSupport` in its initial `initialize` request.
    std::optional<rfl::Variant<bool, RenameOptions>> renameProvider;

    /// The server provides folding provider support.
    std::optional<rfl::Variant<bool, FoldingRangeOptions, FoldingRangeRegistrationOptions>>
        foldingRangeProvider;

    /// The server provides selection range support.
    std::optional<rfl::Variant<bool, SelectionRangeOptions, SelectionRangeRegistrationOptions>>
        selectionRangeProvider;

    /// The server provides execute command support.
    std::optional<ExecuteCommandOptions> executeCommandProvider;

    /// The server provides call hierarchy support.
    ///
    /// @since 3.16.0
    std::optional<rfl::Variant<bool, CallHierarchyOptions, CallHierarchyRegistrationOptions>>
        callHierarchyProvider;

    /// The server provides linked editing range support.
    ///
    /// @since 3.16.0
    std::optional<
        rfl::Variant<bool, LinkedEditingRangeOptions, LinkedEditingRangeRegistrationOptions>>
        linkedEditingRangeProvider;

    /// The server provides semantic tokens support.
    ///
    /// @since 3.16.0
    std::optional<rfl::Variant<SemanticTokensOptions, SemanticTokensRegistrationOptions>>
        semanticTokensProvider;

    /// The server provides moniker support.
    ///
    /// @since 3.16.0
    std::optional<rfl::Variant<bool, MonikerOptions, MonikerRegistrationOptions>> monikerProvider;

    /// The server provides type hierarchy support.
    ///
    /// @since 3.17.0
    std::optional<rfl::Variant<bool, TypeHierarchyOptions, TypeHierarchyRegistrationOptions>>
        typeHierarchyProvider;

    /// The server provides inline values.
    ///
    /// @since 3.17.0
    std::optional<rfl::Variant<bool, InlineValueOptions, InlineValueRegistrationOptions>>
        inlineValueProvider;

    /// The server provides inlay hints.
    ///
    /// @since 3.17.0
    std::optional<rfl::Variant<bool, InlayHintOptions, InlayHintRegistrationOptions>>
        inlayHintProvider;

    /// The server has support for pull model diagnostics.
    ///
    /// @since 3.17.0
    std::optional<rfl::Variant<DiagnosticOptions, DiagnosticRegistrationOptions>>
        diagnosticProvider;

    /// Inline completion options used during static registration.
    ///
    /// @since 3.18.0
    /// @proposed
    std::optional<rfl::Variant<bool, InlineCompletionOptions>> inlineCompletionProvider;

    /// Workspace specific server capabilities.
    std::optional<WorkspaceOptions> workspace;

    /// Experimental server capabilities.
    std::optional<LSPAny> experimental;
};

/// @since 3.16.0
struct SemanticTokensEdit {
    /// The start offset of the edit.
    uint start;

    /// The count of elements to remove.
    uint deleteCount;

    /// The elements to insert.
    std::optional<std::vector<uint>> data;
};

/// An unchanged diagnostic report with a set of related documents.
///
/// @since 3.17.0
struct RelatedUnchangedDocumentDiagnosticReport {
    /// Diagnostics of related documents. This information is useful
    /// in programming languages where code in a file A can generate
    /// diagnostics in a file B which A depends on. An example of
    /// such a language is C/C++ where marco definitions in a file
    /// a.cpp and result in errors in a header file b.hpp.
    ///
    /// @since 3.17.0
    std::optional<std::unordered_map<
        std::string, rfl::Variant<FullDocumentDiagnosticReport, UnchangedDocumentDiagnosticReport>>>
        relatedDocuments;

    /// @inherited from UnchangedDocumentDiagnosticReport
    /// A document diagnostic report indicating
    /// no changes to the last result. A server can
    /// only return `unchanged` if result ids are
    /// provided.
    rfl::Literal<"unchanged"> kind;

    /// @inherited from UnchangedDocumentDiagnosticReport
    /// A result id which will be sent on the next
    /// diagnostic request for the same document.
    std::string resultId;
};

/// A full diagnostic report with a set of related documents.
///
/// @since 3.17.0
struct RelatedFullDocumentDiagnosticReport {
    /// Diagnostics of related documents. This information is useful
    /// in programming languages where code in a file A can generate
    /// diagnostics in a file B which A depends on. An example of
    /// such a language is C/C++ where marco definitions in a file
    /// a.cpp and result in errors in a header file b.hpp.
    ///
    /// @since 3.17.0
    std::optional<std::unordered_map<
        std::string, rfl::Variant<FullDocumentDiagnosticReport, UnchangedDocumentDiagnosticReport>>>
        relatedDocuments;

    /// @inherited from FullDocumentDiagnosticReport
    /// A full document diagnostic report.
    rfl::Literal<"full"> kind;

    /// @inherited from FullDocumentDiagnosticReport
    /// An optional result id. If provided it will
    /// be sent on the next diagnostic request for the
    /// same document.
    std::optional<std::string> resultId;

    /// @inherited from FullDocumentDiagnosticReport
    /// The actual items.
    std::vector<Diagnostic> items;
};

/// General parameters to register for a notification or to register a provider.
struct Registration {
    /// The id used to register the request. The id can be used to deregister
    /// the request again.
    std::string id;

    /// The method / capability to register for.
    std::string method;

    /// Options necessary for the registration.
    std::optional<LSPAny> registerOptions;
};

/// Value-object that contains additional information when
/// requesting references.
struct ReferenceContext {
    /// Include the declaration of the current symbol.
    bool includeDeclaration;
};

/// A previous result id in a workspace pull request.
///
/// @since 3.17.0
struct PreviousResultId {
    /// The URI for which the client knowns a
    /// result id.
    URI uri;

    /// The value of the previous result id.
    std::string value;
};

/// @since 3.18.0
struct PrepareRenamePlaceholder {
    Range range;

    std::string placeholder;
};

/// @since 3.18.0
struct PrepareRenameDefaultBehavior {
    bool defaultBehavior;
};

struct PartialResultParams {
    /// An optional token that a server can use to report partial results (e.g. streaming) to
    /// the client.
    std::optional<ProgressToken> partialResultToken;
};

/// A literal to identify a notebook document in the client.
///
/// @since 3.17.0
struct NotebookDocumentIdentifier {
    /// The notebook document's uri.
    URI uri;
};

/// A change event for a notebook document.
///
/// @since 3.17.0
struct NotebookDocumentChangeEvent {
    /// The changed meta data if any.
    ///
    /// Note: should always be an object literal (e.g. LSPObject)
    std::optional<LSPObject> metadata;

    /// Changes to cells
    std::optional<NotebookDocumentCellChanges> cells;
};

/// A notebook document.
///
/// @since 3.17.0
struct NotebookDocument {
    /// The notebook document's uri.
    URI uri;

    /// The type of the notebook.
    std::string notebookType;

    /// The version number of this document (it will increase after each
    /// change, including undo/redo).
    int version;

    /// Additional metadata stored with the notebook
    /// document.
    ///
    /// Note: should always be an object literal (e.g. LSPObject)
    std::optional<LSPObject> metadata;

    /// The cells of a notebook.
    std::vector<NotebookCell> cells;
};

/// The moniker kind.
///
/// @since 3.16.0
using MonikerKind = rfl::Literal<"import", "export", "local">;

/// The message type
enum class MessageType : uint8_t {
    Error = 1,
    Warning = 2,
    Info = 3,
    Log = 4,
    Debug = 5,
};

struct MessageActionItem {
    /// A short title like 'Retry', 'Open Log' etc.
    std::string title;
};

/// MarkedString can be used to render human readable text. It is either a markdown string
/// or a code-block that provides a language and a code snippet. The language identifier
/// is semantically equal to the optional language identifier in fenced code blocks in GitHub
/// issues. See
/// https://help.github.com/articles/creating-and-highlighting-code-blocks/#syntax-highlighting
///
/// The pair of a language and a value is an equivalent to markdown:
/// ```${language}
/// ${value}
/// ```
///
/// Note that markdown strings will be sanitized - that means html will be escaped.
/// @deprecated use MarkupContent instead.
using MarkedString = rfl::Variant<std::string, MarkedStringWithLanguage>;

/// Location with only uri and does not include range.
///
/// @since 3.18.0
struct LocationUriOnly {
    URI uri;
};

/// Represents the connection of two locations. Provides additional metadata over normal {@link
/// Location locations}, including an origin range.
struct LocationLink {
    /// Span of the origin of this link.
    ///
    /// Used as the underlined span for mouse interaction. Defaults to the word range at
    /// the definition position.
    std::optional<Range> originSelectionRange;

    /// The target resource identifier of this link.
    URI targetUri;

    /// The full target range of this link. If the target for example is a symbol then target range
    /// is the range enclosing this symbol not including leading/trailing whitespace but everything
    /// else like comments. This information is typically used to highlight the range in the editor.
    Range targetRange;

    /// The range that should be selected and revealed when this link is being followed, e.g the
    /// name of a function. Must be contained by the `targetRange`. See also `DocumentSymbol#range`
    Range targetSelectionRange;
};

/// Provide inline value through a variable lookup.
/// If only a range is specified, the variable name will be extracted from the underlying document.
/// An optional variable name can be used to override the extracted name.
///
/// @since 3.17.0
struct InlineValueVariableLookup {
    /// The document range for which the inline value applies.
    /// The range is used to extract the variable name from the underlying document.
    Range range;

    /// If specified the name of the variable to look up.
    std::optional<std::string> variableName;

    /// How to perform the lookup.
    bool caseSensitiveLookup;
};

/// Provide inline value as text.
///
/// @since 3.17.0
struct InlineValueText {
    /// The document range for which the inline value applies.
    Range range;

    /// The text of the inline value.
    std::string text;
};

/// Provide an inline value through an expression evaluation.
/// If only a range is specified, the expression will be extracted from the underlying document.
/// An optional expression can be used to override the extracted expression.
///
/// @since 3.17.0
struct InlineValueEvaluatableExpression {
    /// The document range for which the inline value applies.
    /// The range is used to extract the evaluatable expression from the underlying document.
    Range range;

    /// If specified the expression overrides the extracted expression.
    std::optional<std::string> expression;
};

/// @since 3.17.0
struct InlineValueContext {
    /// The stack frame (as a DAP Id) where the execution has stopped.
    int frameId;

    /// The document range where execution has stopped.
    /// Typically the end position of the range denotes the line where the inline values are shown.
    Range stoppedLocation;
};

/// An inline completion item represents a text snippet that is proposed inline to complete text
/// that is being typed.
///
/// @since 3.18.0
/// @proposed
struct InlineCompletionItem {
    /// The text to replace the range with. Must be set.
    rfl::Variant<std::string, StringValue> insertText;

    /// A text that is used to decide if this inline completion should be shown. When `falsy` the
    /// {@link InlineCompletionItem.insertText} is used.
    std::optional<std::string> filterText;

    /// The range to replace. Must begin and end on the same line.
    std::optional<Range> range;

    /// An optional {@link Command} that is executed *after* inserting this completion.
    std::optional<Command> command;
};

/// Provides information about the context in which an inline completion was requested.
///
/// @since 3.18.0
/// @proposed
struct InlineCompletionContext {
    /// Describes how the inline completion was triggered.
    InlineCompletionTriggerKind triggerKind;

    /// Provides information about the currently selected item in the autocomplete widget if it is
    /// visible.
    std::optional<SelectedCompletionInfo> selectedCompletionInfo;
};

/// An inlay hint label part allows for interactive and composite labels
/// of inlay hints.
///
/// @since 3.17.0
struct InlayHintLabelPart {
    /// The value of this label part.
    std::string value;

    /// The tooltip text when you hover over this label part. Depending on
    /// the client capability `inlayHint.resolveSupport` clients might resolve
    /// this property late using the resolve request.
    std::optional<rfl::Variant<std::string, MarkupContent>> tooltip;

    /// An optional source code location that represents this
    /// label part.
    ///
    /// The editor will use this location for the hover and for code navigation
    /// features: This part will become a clickable link that resolves to the
    /// definition of the symbol at the given location (not necessarily the
    /// location itself), it shows the hover that shows at the given location,
    /// and it shows a context menu with further code navigation commands.
    ///
    /// Depending on the client capability `inlayHint.resolveSupport` clients
    /// might resolve this property late using the resolve request.
    std::optional<Location> location;

    /// An optional command for this label part.
    ///
    /// Depending on the client capability `inlayHint.resolveSupport` clients
    /// might resolve this property late using the resolve request.
    std::optional<Command> command;
};

/// Inlay hint kinds.
///
/// @since 3.17.0
enum class InlayHintKind : uint8_t {
    Type = 1,
    Parameter = 2,
};

/// Value-object describing what options formatting should use.
struct FormattingOptions {
    /// Size of a tab in spaces.
    uint tabSize;

    /// Prefer spaces over tabs.
    bool insertSpaces;

    /// Trim trailing whitespace on a line.
    ///
    /// @since 3.15.0
    std::optional<bool> trimTrailingWhitespace;

    /// Insert a newline character at the end of the file if one does not exist.
    ///
    /// @since 3.15.0
    std::optional<bool> insertFinalNewline;

    /// Trim all newlines after the final newline at the end of the file.
    ///
    /// @since 3.15.0
    std::optional<bool> trimFinalNewlines;
};

/// Represents information on a file/folder rename.
///
/// @since 3.16.0
struct FileRename {
    /// A file:// URI for the original location of the file/folder being renamed.
    std::string oldUri;

    /// A file:// URI for the new location of the file/folder being renamed.
    std::string newUri;
};

/// An event describing a file change.
struct FileEvent {
    /// The file's uri.
    URI uri;

    /// The change type.
    FileChangeType type;
};

/// Represents information on a file/folder delete.
///
/// @since 3.16.0
struct FileDelete {
    /// A file:// URI for the location of the file/folder being deleted.
    std::string uri;
};

/// Represents information on a file/folder create.
///
/// @since 3.16.0
struct FileCreate {
    /// A file:// URI for the location of the file/folder being created.
    std::string uri;
};

/// A document highlight kind.
enum class DocumentHighlightKind : uint8_t {
    Text = 1,
    Read = 2,
    Write = 3,
};

struct ConfigurationItem {
    /// The scope to get the configuration section for.
    std::optional<URI> scopeUri;

    /// The configuration section asked for.
    std::optional<std::string> section;
};

/// In many cases the items of an actual completion result share the same
/// value for properties like `commitCharacters` or the range of a text
/// edit. A completion list can therefore define item defaults which will
/// be used if a completion item itself doesn't specify the value.
///
/// If a completion list specifies a default value and a completion item
/// also specifies a corresponding value, the rules for combining these are
/// defined by `applyKinds` (if the client supports it), defaulting to
/// ApplyKind.Replace.
///
/// Servers are only allowed to return default values if the client
/// signals support for this via the `completionList.itemDefaults`
/// capability.
///
/// @since 3.17.0
struct CompletionItemDefaults {
    /// A default commit character set.
    ///
    /// @since 3.17.0
    std::optional<std::vector<std::string>> commitCharacters;

    /// A default edit range.
    ///
    /// @since 3.17.0
    std::optional<rfl::Variant<Range, EditRangeWithInsertReplace>> editRange;

    /// A default insert text format.
    ///
    /// @since 3.17.0
    std::optional<InsertTextFormat> insertTextFormat;

    /// A default insert text mode.
    ///
    /// @since 3.17.0
    std::optional<InsertTextMode> insertTextMode;

    /// A default data value.
    ///
    /// @since 3.17.0
    std::optional<LSPAny> data;
};

/// Specifies how fields from a completion item should be combined with those
/// from `completionList.itemDefaults`.
///
/// If unspecified, all fields will be treated as ApplyKind.Replace.
///
/// If a field's value is ApplyKind.Replace, the value from a completion item (if
/// provided and not `null`) will always be used instead of the value from
/// `completionItem.itemDefaults`.
///
/// If a field's value is ApplyKind.Merge, the values will be merged using the rules
/// defined against each field below.
///
/// Servers are only allowed to return `applyKind` if the client
/// signals support for this via the `completionList.applyKindSupport`
/// capability.
///
/// @since 3.18.0
struct CompletionItemApplyKinds {
    /// Specifies whether commitCharacters on a completion will replace or be
    /// merged with those in `completionList.itemDefaults.commitCharacters`.
    ///
    /// If ApplyKind.Replace, the commit characters from the completion item will
    /// always be used unless not provided, in which case those from
    /// `completionList.itemDefaults.commitCharacters` will be used. An
    /// empty list can be used if a completion item does not have any commit
    /// characters and also should not use those from
    /// `completionList.itemDefaults.commitCharacters`.
    ///
    /// If ApplyKind.Merge the commitCharacters for the completion will be the
    /// union of all values in both `completionList.itemDefaults.commitCharacters`
    /// and the completion's own `commitCharacters`.
    ///
    /// @since 3.18.0
    std::optional<ApplyKind> commitCharacters;

    /// Specifies whether the `data` field on a completion will replace or
    /// be merged with data from `completionList.itemDefaults.data`.
    ///
    /// If ApplyKind.Replace, the data from the completion item will be used if
    /// provided (and not `null`), otherwise
    /// `completionList.itemDefaults.data` will be used. An empty object can
    /// be used if a completion item does not have any data but also should
    /// not use the value from `completionList.itemDefaults.data`.
    ///
    /// If ApplyKind.Merge, a shallow merge will be performed between
    /// `completionList.itemDefaults.data` and the completion's own data
    /// using the following rules:
    ///
    /// - If a completion's `data` field is not provided (or `null`), the
    ///   entire `data` field from `completionList.itemDefaults.data` will be
    ///   used as-is.
    /// - If a completion's `data` field is provided, each field will
    ///   overwrite the field of the same name in
    ///   `completionList.itemDefaults.data` but no merging of nested fields
    ///   within that value will occur.
    ///
    /// @since 3.18.0
    std::optional<ApplyKind> data;
};

/// A completion item represents a text snippet that is
/// proposed to complete text that is being typed.
struct CompletionItem {
    /// The label of this completion item.
    ///
    /// The label property is also by default the text that
    /// is inserted when selecting this completion.
    ///
    /// If label details are provided the label itself should
    /// be an unqualified name of the completion item.
    std::string label;

    /// Additional details for the label
    ///
    /// @since 3.17.0
    std::optional<CompletionItemLabelDetails> labelDetails;

    /// The kind of this completion item. Based of the kind
    /// an icon is chosen by the editor.
    std::optional<CompletionItemKind> kind;

    /// Tags for this completion item.
    ///
    /// @since 3.15.0
    std::optional<std::vector<CompletionItemTag>> tags;

    /// A human-readable string with additional information
    /// about this item, like type or symbol information.
    std::optional<std::string> detail;

    /// A human-readable string that represents a doc-comment.
    std::optional<rfl::Variant<std::string, MarkupContent>> documentation;

    /// Indicates if this item is deprecated.
    /// @deprecated Use `tags` instead.
    std::optional<bool> deprecated;

    /// Select this item when showing.
    ///
    /// *Note* that only one completion item can be selected and that the
    /// tool / client decides which item that is. The rule is that the *first*
    /// item of those that match best is selected.
    std::optional<bool> preselect;

    /// A string that should be used when comparing this item
    /// with other items. When `falsy` the {@link CompletionItem.label label}
    /// is used.
    std::optional<std::string> sortText;

    /// A string that should be used when filtering a set of
    /// completion items. When `falsy` the {@link CompletionItem.label label}
    /// is used.
    std::optional<std::string> filterText;

    /// A string that should be inserted into a document when selecting
    /// this completion. When `falsy` the {@link CompletionItem.label label}
    /// is used.
    ///
    /// The `insertText` is subject to interpretation by the client side.
    /// Some tools might not take the string literally. For example
    /// VS Code when code complete is requested in this example
    /// `con<cursor position>` and a completion item with an `insertText` of
    /// `console` is provided it will only insert `sole`. Therefore it is
    /// recommended to use `textEdit` instead since it avoids additional client
    /// side interpretation.
    std::optional<std::string> insertText;

    /// The format of the insert text. The format applies to both the
    /// `insertText` property and the `newText` property of a provided
    /// `textEdit`. If omitted defaults to `InsertTextFormat.PlainText`.
    ///
    /// Please note that the insertTextFormat doesn't apply to
    /// `additionalTextEdits`.
    std::optional<InsertTextFormat> insertTextFormat;

    /// How whitespace and indentation is handled during completion
    /// item insertion. If not provided the clients default value depends on
    /// the `textDocument.completion.insertTextMode` client capability.
    ///
    /// @since 3.16.0
    std::optional<InsertTextMode> insertTextMode;

    /// An {@link TextEdit edit} which is applied to a document when selecting
    /// this completion. When an edit is provided the value of
    /// {@link CompletionItem.insertText insertText} is ignored.
    ///
    /// Most editors support two different operations when accepting a completion
    /// item. One is to insert a completion text and the other is to replace an
    /// existing text with a completion text. Since this can usually not be
    /// predetermined by a server it can report both ranges. Clients need to
    /// signal support for `InsertReplaceEdits` via the
    /// `textDocument.completion.insertReplaceSupport` client capability
    /// property.
    ///
    /// *Note 1:* The text edit's range as well as both ranges from an insert
    /// replace edit must be a [single line] and they must contain the position
    /// at which completion has been requested.
    /// *Note 2:* If an `InsertReplaceEdit` is returned the edit's insert range
    /// must be a prefix of the edit's replace range, that means it must be
    /// contained and starting at the same position.
    ///
    /// @since 3.16.0 additional type `InsertReplaceEdit`
    std::optional<rfl::Variant<TextEdit, InsertReplaceEdit>> textEdit;

    /// The edit text used if the completion item is part of a CompletionList and
    /// CompletionList defines an item default for the text edit range.
    ///
    /// Clients will only honor this property if they opt into completion list
    /// item defaults using the capability `completionList.itemDefaults`.
    ///
    /// If not provided and a list's default range is provided the label
    /// property is used as a text.
    ///
    /// @since 3.17.0
    std::optional<std::string> textEditText;

    /// An optional array of additional {@link TextEdit text edits} that are applied when
    /// selecting this completion. Edits must not overlap (including the same insert position)
    /// with the main {@link CompletionItem.textEdit edit} nor with themselves.
    ///
    /// Additional text edits should be used to change text unrelated to the current cursor position
    /// (for example adding an import statement at the top of the file if the completion item will
    /// insert an unqualified type).
    std::optional<std::vector<TextEdit>> additionalTextEdits;

    /// An optional set of characters that when pressed while this completion is active will accept
    /// it first and then type that character. *Note* that all commit characters should have
    /// `length=1` and that superfluous characters will be ignored.
    std::optional<std::vector<std::string>> commitCharacters;

    /// An optional {@link Command command} that is executed *after* inserting this completion.
    /// *Note* that additional modifications to the current document should be described with the
    /// {@link CompletionItem.additionalTextEdits additionalTextEdits}-property.
    std::optional<Command> command;

    /// A data entry field that is preserved on a completion item between a
    /// {@link CompletionRequest} and a {@link CompletionResolveRequest}.
    std::optional<LSPAny> data;
};

/// Contains additional information about the context in which a completion request is triggered.
struct CompletionContext {
    /// How the completion was triggered.
    CompletionTriggerKind triggerKind;

    /// The trigger character (a single character) that has trigger code complete.
    /// Is undefined if `triggerKind !== CompletionTriggerKind.TriggerCharacter`
    std::optional<std::string> triggerCharacter;
};

/// Represents a color in RGBA space.
struct Color {
    /// The red component of this color in the range [0-1].
    float red;

    /// The green component of this color in the range [0-1].
    float green;

    /// The blue component of this color in the range [0-1].
    float blue;

    /// The alpha component of this color in the range [0-1].
    float alpha;
};

/// Captures why the code action is currently disabled.
///
/// @since 3.18.0
struct CodeActionDisabled {
    /// Human readable description of why the code action is currently disabled.
    ///
    /// This is displayed in the code actions UI.
    std::string reason;
};

/// Contains additional diagnostic information about the context in which
/// a {@link CodeActionProvider.provideCodeActions code action} is run.
struct CodeActionContext {
    /// An array of diagnostics known on the client side overlapping the range provided to the
    /// `textDocument/codeAction` request. They are provided so that the server knows which
    /// errors are currently presented to the user for the given range. There is no guarantee
    /// that these accurately reflect the error state of the resource. The primary parameter
    /// to compute code actions is the provided range.
    std::vector<Diagnostic> diagnostics;

    /// Requested kind of actions to return.
    ///
    /// Actions not of this kind are filtered out by the client before being shown. So servers
    /// can omit computing them.
    std::optional<std::vector<CodeActionKind>> only;

    /// The reason why code actions were requested.
    ///
    /// @since 3.17.0
    std::optional<CodeActionTriggerKind> triggerKind;
};

/// Represents programming constructs like functions or constructors in the context
/// of call hierarchy.
///
/// @since 3.16.0
struct CallHierarchyItem {
    /// The name of this item.
    std::string name;

    /// The kind of this item.
    SymbolKind kind;

    /// Tags for this item.
    std::optional<std::vector<SymbolTag>> tags;

    /// More detail for this item, e.g. the signature of a function.
    std::optional<std::string> detail;

    /// The resource identifier of this item.
    URI uri;

    /// The range enclosing this symbol not including leading/trailing whitespace but everything
    /// else, e.g. comments and code.
    Range range;

    /// The range that should be selected and revealed when this symbol is being picked, e.g. the
    /// name of a function. Must be contained by the {@link CallHierarchyItem.range `range`}.
    Range selectionRange;

    /// A data entry field that is preserved between a call hierarchy prepare and
    /// incoming calls or outgoing calls requests.
    std::optional<LSPAny> data;
};

/// A base for all symbol information.
struct BaseSymbolInformation {
    /// The name of this symbol.
    std::string name;

    /// The kind of this symbol.
    SymbolKind kind;

    /// Tags for this symbol.
    ///
    /// @since 3.16.0
    std::optional<std::vector<SymbolTag>> tags;

    /// The name of the symbol containing this symbol. This information is for
    /// user interface purposes (e.g. to render a qualifier in the user interface
    /// if necessary). It can't be used to re-infer a hierarchy for the document
    /// symbols.
    std::optional<std::string> containerName;
};

/// The parameters of a {@link WorkspaceSymbolRequest}.
struct WorkspaceSymbolParams {
    /// A query string to filter symbols by. Clients may send an empty
    /// string here to request all symbols.
    ///
    /// The `query`-parameter should be interpreted in a *relaxed way* as editors
    /// will apply their own highlighting and scoring on the results. A good rule
    /// of thumb is to match case-insensitive and to simply check that the
    /// characters of *query* appear in their order in a candidate symbol.
    /// Servers shouldn't use prefix, substring, or similar strict matching.
    std::string query;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;

    /// @mixin from PartialResultParams
    /// An optional token that a server can use to report partial results (e.g. streaming) to
    /// the client.
    std::optional<ProgressToken> partialResultToken;
};

/// A special workspace symbol that supports locations without a range.
///
/// See also SymbolInformation.
///
/// @since 3.17.0
struct WorkspaceSymbol {
    /// The location of the symbol. Whether a server is allowed to
    /// return a location without a range depends on the client
    /// capability `workspace.symbol.resolveSupport`.
    ///
    /// See SymbolInformation#location for more details.
    rfl::Variant<Location, LocationUriOnly> location;

    /// A data entry field that is preserved on a workspace symbol between a
    /// workspace symbol request and a workspace symbol resolve request.
    std::optional<LSPAny> data;

    /// @inherited from BaseSymbolInformation
    /// The name of this symbol.
    std::string name;

    /// @inherited from BaseSymbolInformation
    /// The kind of this symbol.
    SymbolKind kind;

    /// @inherited from BaseSymbolInformation
    /// Tags for this symbol.
    ///
    /// @since 3.16.0
    std::optional<std::vector<SymbolTag>> tags;

    /// @inherited from BaseSymbolInformation
    /// The name of the symbol containing this symbol. This information is for
    /// user interface purposes (e.g. to render a qualifier in the user interface
    /// if necessary). It can't be used to re-infer a hierarchy for the document
    /// symbols.
    std::optional<std::string> containerName;
};

/// A partial result for a workspace diagnostic report.
///
/// @since 3.17.0
struct WorkspaceDiagnosticReportPartialResult {
    std::vector<WorkspaceDocumentDiagnosticReport> items;
};

/// A workspace diagnostic report.
///
/// @since 3.17.0
struct WorkspaceDiagnosticReport {
    std::vector<WorkspaceDocumentDiagnosticReport> items;
};

/// Parameters of the workspace diagnostic request.
///
/// @since 3.17.0
struct WorkspaceDiagnosticParams {
    /// The additional identifier provided during registration.
    std::optional<std::string> identifier;

    /// The currently known diagnostic reports with their
    /// previous result ids.
    std::vector<PreviousResultId> previousResultIds;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;

    /// @mixin from PartialResultParams
    /// An optional token that a server can use to report partial results (e.g. streaming) to
    /// the client.
    std::optional<ProgressToken> partialResultToken;
};

struct WorkDoneProgressCreateParams {
    /// The token to be used to report progress.
    ProgressToken token;
};

struct WorkDoneProgressCancelParams {
    /// The token to be used to report progress.
    ProgressToken token;
};

/// The parameters sent in a will save text document notification.
struct WillSaveTextDocumentParams {
    /// The document that will be saved.
    TextDocumentIdentifier textDocument;

    /// The 'TextDocumentSaveReason'.
    TextDocumentSaveReason reason;
};

struct UnregistrationParams {
    std::vector<Unregistration> unregisterations;
};

/// The parameter of a `typeHierarchy/supertypes` request.
///
/// @since 3.17.0
struct TypeHierarchySupertypesParams {
    TypeHierarchyItem item;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;

    /// @mixin from PartialResultParams
    /// An optional token that a server can use to report partial results (e.g. streaming) to
    /// the client.
    std::optional<ProgressToken> partialResultToken;
};

/// The parameter of a `typeHierarchy/subtypes` request.
///
/// @since 3.17.0
struct TypeHierarchySubtypesParams {
    TypeHierarchyItem item;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;

    /// @mixin from PartialResultParams
    /// An optional token that a server can use to report partial results (e.g. streaming) to
    /// the client.
    std::optional<ProgressToken> partialResultToken;
};

/// The parameter of a `textDocument/prepareTypeHierarchy` request.
///
/// @since 3.17.0
struct TypeHierarchyPrepareParams {
    /// @inherited from TextDocumentPositionParams
    /// The text document.
    TextDocumentIdentifier textDocument;

    /// @inherited from TextDocumentPositionParams
    /// The position inside the text document.
    Position position;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;
};

struct TypeDefinitionParams {
    /// @inherited from TextDocumentPositionParams
    /// The text document.
    TextDocumentIdentifier textDocument;

    /// @inherited from TextDocumentPositionParams
    /// The position inside the text document.
    Position position;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;

    /// @mixin from PartialResultParams
    /// An optional token that a server can use to report partial results (e.g. streaming) to
    /// the client.
    std::optional<ProgressToken> partialResultToken;
};

/// Result of the `workspace/textDocumentContent` request.
///
/// @since 3.18.0
/// @proposed
struct TextDocumentContentResult {
    /// The text content of the text document. Please note, that the content of
    /// any subsequent open notifications for the text document might differ
    /// from the returned content due to whitespace and line ending
    /// normalizations done on the client
    std::string text;
};

/// Parameters for the `workspace/textDocumentContent/refresh` request.
///
/// @since 3.18.0
/// @proposed
struct TextDocumentContentRefreshParams {
    /// The uri of the text document to refresh.
    URI uri;
};

/// Parameters for the `workspace/textDocumentContent` request.
///
/// @since 3.18.0
/// @proposed
struct TextDocumentContentParams {
    /// The uri of the text document.
    URI uri;
};

/// Represents information about programming constructs like variables, classes,
/// interfaces etc.
struct SymbolInformation {
    /// Indicates if this symbol is deprecated.
    ///
    /// @deprecated Use tags instead
    std::optional<bool> deprecated;

    /// The location of this symbol. The location's range is used by a tool
    /// to reveal the location in the editor. If the symbol is selected in the
    /// tool the range's start information is used to position the cursor. So
    /// the range usually spans more than the actual symbol's name and does
    /// normally include things like visibility modifiers.
    ///
    /// The range doesn't have to denote a node range in the sense of an abstract
    /// syntax tree. It can therefore not be used to re-construct a hierarchy of
    /// the symbols.
    Location location;

    /// @inherited from BaseSymbolInformation
    /// The name of this symbol.
    std::string name;

    /// @inherited from BaseSymbolInformation
    /// The kind of this symbol.
    SymbolKind kind;

    /// @inherited from BaseSymbolInformation
    /// Tags for this symbol.
    ///
    /// @since 3.16.0
    std::optional<std::vector<SymbolTag>> tags;

    /// @inherited from BaseSymbolInformation
    /// The name of the symbol containing this symbol. This information is for
    /// user interface purposes (e.g. to render a qualifier in the user interface
    /// if necessary). It can't be used to re-infer a hierarchy for the document
    /// symbols.
    std::optional<std::string> containerName;
};

/// Parameters for a {@link SignatureHelpRequest}.
struct SignatureHelpParams {
    /// The signature help context. This is only available if the client specifies
    /// to send this using the client capability `textDocument.signatureHelp.contextSupport ===
    /// true`
    ///
    /// @since 3.15.0
    std::optional<SignatureHelpContext> context;

    /// @inherited from TextDocumentPositionParams
    /// The text document.
    TextDocumentIdentifier textDocument;

    /// @inherited from TextDocumentPositionParams
    /// The position inside the text document.
    Position position;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;
};

struct ShowMessageRequestParams {
    /// The message type. See {@link MessageType}
    MessageType type;

    /// The actual message.
    std::string message;

    /// The message action items to present.
    std::optional<std::vector<MessageActionItem>> actions;
};

/// The parameters of a notification message.
struct ShowMessageParams {
    /// The message type. See {@link MessageType}
    MessageType type;

    /// The actual message.
    std::string message;
};

/// The result of a showDocument request.
///
/// @since 3.16.0
struct ShowDocumentResult {
    /// A boolean indicating if the show was successful.
    bool success;
};

/// Params to show a resource in the UI.
///
/// @since 3.16.0
struct ShowDocumentParams {
    /// The uri to show.
    URI uri;

    /// Indicates to show the resource in an external program.
    /// To show, for example, `https://code.visualstudio.com/`
    /// in the default WEB browser set `external` to `true`.
    std::optional<bool> external;

    /// An optional property to indicate whether the editor
    /// showing the document should take focus or not.
    /// Clients might ignore this property if an external
    /// program is started.
    std::optional<bool> takeFocus;

    /// An optional selection range if the document is a text
    /// document. Clients might ignore the property if an
    /// external program is started or the file is not a text
    /// file.
    std::optional<Range> selection;
};

struct SetTraceParams {
    TraceValue value;
};

/// @since 3.16.0
struct SemanticTokensRangeParams {
    /// The text document.
    TextDocumentIdentifier textDocument;

    /// The range the semantic tokens are requested for.
    Range range;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;

    /// @mixin from PartialResultParams
    /// An optional token that a server can use to report partial results (e.g. streaming) to
    /// the client.
    std::optional<ProgressToken> partialResultToken;
};

/// @since 3.16.0
struct SemanticTokensPartialResult {
    std::vector<uint> data;
};

/// @since 3.16.0
struct SemanticTokensParams {
    /// The text document.
    TextDocumentIdentifier textDocument;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;

    /// @mixin from PartialResultParams
    /// An optional token that a server can use to report partial results (e.g. streaming) to
    /// the client.
    std::optional<ProgressToken> partialResultToken;
};

/// @since 3.16.0
struct SemanticTokensDeltaPartialResult {
    std::vector<SemanticTokensEdit> edits;
};

/// @since 3.16.0
struct SemanticTokensDeltaParams {
    /// The text document.
    TextDocumentIdentifier textDocument;

    /// The result id of a previous response. The result Id can either point to a full response
    /// or a delta response depending on what was received last.
    std::string previousResultId;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;

    /// @mixin from PartialResultParams
    /// An optional token that a server can use to report partial results (e.g. streaming) to
    /// the client.
    std::optional<ProgressToken> partialResultToken;
};

/// @since 3.16.0
struct SemanticTokensDelta {
    std::optional<std::string> resultId;

    /// The semantic token edits to transform a previous result into a new result.
    std::vector<SemanticTokensEdit> edits;
};

/// @since 3.16.0
struct SemanticTokens {
    /// An optional result id. If provided and clients support delta updating
    /// the client will include the result id in the next semantic token request.
    /// A server can then instead of computing all semantic tokens again simply
    /// send a delta.
    std::optional<std::string> resultId;

    /// The actual tokens.
    std::vector<uint> data;
};

/// A parameter literal used in selection range requests.
struct SelectionRangeParams {
    /// The text document.
    TextDocumentIdentifier textDocument;

    /// The positions inside the text document.
    std::vector<Position> positions;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;

    /// @mixin from PartialResultParams
    /// An optional token that a server can use to report partial results (e.g. streaming) to
    /// the client.
    std::optional<ProgressToken> partialResultToken;
};

/// A selection range represents a part of a selection hierarchy. A selection range
/// may have a parent selection range that contains it.
struct SelectionRange {
    /// The {@link Range range} of this selection range.
    Range range;

    /// The parent selection range containing this range. Therefore `parent.range` must contain
    /// `this.range`.
    rfl::Box<SelectionRange> parent;
};

/// The parameters of a {@link RenameRequest}.
struct RenameParams {
    /// The document to rename.
    TextDocumentIdentifier textDocument;

    /// The position at which this request was sent.
    Position position;

    /// The new name of the symbol. If the given name is not valid the
    /// request must return a {@link ResponseError} with an
    /// appropriate message set.
    std::string newName;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;
};

/// The parameters sent in notifications/requests for user-initiated renames of
/// files.
///
/// @since 3.16.0
struct RenameFilesParams {
    /// An array of all files/folders renamed in this operation. When a folder is renamed, only
    /// the folder will be included, and not its children.
    std::vector<FileRename> files;
};

struct RegistrationParams {
    std::vector<Registration> registrations;
};

/// Parameters for a {@link ReferencesRequest}.
struct ReferenceParams {
    ReferenceContext context;

    /// @inherited from TextDocumentPositionParams
    /// The text document.
    TextDocumentIdentifier textDocument;

    /// @inherited from TextDocumentPositionParams
    /// The position inside the text document.
    Position position;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;

    /// @mixin from PartialResultParams
    /// An optional token that a server can use to report partial results (e.g. streaming) to
    /// the client.
    std::optional<ProgressToken> partialResultToken;
};

/// The publish diagnostic notification's parameters.
struct PublishDiagnosticsParams {
    /// The URI for which diagnostic information is reported.
    URI uri;

    /// Optional the version number of the document the diagnostics are published for.
    ///
    /// @since 3.15.0
    std::optional<int> version;

    /// An array of diagnostic information items.
    std::vector<Diagnostic> diagnostics;
};

struct ProgressParams {
    /// The progress token provided by the client or server.
    ProgressToken token;

    /// The progress data.
    LSPAny value;
};

using PrepareRenameResult =
    rfl::Variant<Range, PrepareRenamePlaceholder, PrepareRenameDefaultBehavior>;

struct PrepareRenameParams {
    /// @inherited from TextDocumentPositionParams
    /// The text document.
    TextDocumentIdentifier textDocument;

    /// @inherited from TextDocumentPositionParams
    /// The position inside the text document.
    Position position;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;
};

struct MonikerParams {
    /// @inherited from TextDocumentPositionParams
    /// The text document.
    TextDocumentIdentifier textDocument;

    /// @inherited from TextDocumentPositionParams
    /// The position inside the text document.
    Position position;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;

    /// @mixin from PartialResultParams
    /// An optional token that a server can use to report partial results (e.g. streaming) to
    /// the client.
    std::optional<ProgressToken> partialResultToken;
};

/// Moniker definition to match LSIF 0.5 moniker definition.
///
/// @since 3.16.0
struct Moniker {
    /// The scheme of the moniker. For example tsc or .Net
    std::string scheme;

    /// The identifier of the moniker. The value is opaque in LSIF however
    /// schema owners are allowed to define the structure if they want.
    std::string identifier;

    /// The scope in which the moniker is unique
    UniquenessLevel unique;

    /// The moniker kind if known.
    std::optional<MonikerKind> kind;
};

struct LogTraceParams {
    std::string message;

    std::optional<std::string> verbose;
};

/// The log message parameters.
struct LogMessageParams {
    /// The message type. See {@link MessageType}
    MessageType type;

    /// The actual message.
    std::string message;
};

/// The result of a linked editing range request.
///
/// @since 3.16.0
struct LinkedEditingRanges {
    /// A list of ranges that can be edited together. The ranges must have
    /// identical length and contain identical text content. The ranges cannot overlap.
    std::vector<Range> ranges;

    /// An optional word pattern (regular expression) that describes valid contents for
    /// the given ranges. If no pattern is provided, the client configuration's word
    /// pattern will be used.
    std::optional<std::string> wordPattern;
};

struct LinkedEditingRangeParams {
    /// @inherited from TextDocumentPositionParams
    /// The text document.
    TextDocumentIdentifier textDocument;

    /// @inherited from TextDocumentPositionParams
    /// The position inside the text document.
    Position position;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;
};

/// A parameter literal used in inline value requests.
///
/// @since 3.17.0
struct InlineValueParams {
    /// The text document.
    TextDocumentIdentifier textDocument;

    /// The document range for which inline values should be computed.
    Range range;

    /// Additional information about the context in which inline values were
    /// requested.
    InlineValueContext context;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;
};

/// Inline value information can be provided by different means:
/// - directly as a text value (class InlineValueText).
/// - as a name to use for a variable lookup (class InlineValueVariableLookup)
/// - as an evaluatable expression (class InlineValueEvaluatableExpression)
/// The InlineValue types combines all inline value types into one type.
///
/// @since 3.17.0
using InlineValue =
    rfl::Variant<InlineValueText, InlineValueVariableLookup, InlineValueEvaluatableExpression>;

/// A parameter literal used in inline completion requests.
///
/// @since 3.18.0
/// @proposed
struct InlineCompletionParams {
    /// Additional information about the context in which inline completions were
    /// requested.
    InlineCompletionContext context;

    /// @inherited from TextDocumentPositionParams
    /// The text document.
    TextDocumentIdentifier textDocument;

    /// @inherited from TextDocumentPositionParams
    /// The position inside the text document.
    Position position;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;
};

/// Represents a collection of {@link InlineCompletionItem inline completion items} to be presented
/// in the editor.
///
/// @since 3.18.0
/// @proposed
struct InlineCompletionList {
    /// The inline completion items
    std::vector<InlineCompletionItem> items;
};

/// A parameter literal used in inlay hint requests.
///
/// @since 3.17.0
struct InlayHintParams {
    /// The text document.
    TextDocumentIdentifier textDocument;

    /// The document range for which inlay hints should be computed.
    Range range;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;
};

/// Inlay hint information.
///
/// @since 3.17.0
struct InlayHint {
    /// The position of this hint.
    ///
    /// If multiple hints have the same position, they will be shown in the order
    /// they appear in the response.
    Position position;

    /// The label of this hint. A human readable string or an array of
    /// InlayHintLabelPart label parts.
    ///
    /// *Note* that neither the string nor the label part can be empty.
    rfl::Variant<std::string, std::vector<InlayHintLabelPart>> label;

    /// The kind of this hint. Can be omitted in which case the client
    /// should fall back to a reasonable default.
    std::optional<InlayHintKind> kind;

    /// Optional text edits that are performed when accepting this inlay hint.
    ///
    /// *Note* that edits are expected to change the document so that the inlay
    /// hint (or its nearest variant) is now part of the document and the inlay
    /// hint itself is now obsolete.
    std::optional<std::vector<TextEdit>> textEdits;

    /// The tooltip text when you hover over this item.
    std::optional<rfl::Variant<std::string, MarkupContent>> tooltip;

    /// Render padding before the hint.
    ///
    /// Note: Padding should use the editor's background color, not the
    /// background color of the hint itself. That means padding can be used
    /// to visually align/separate an inlay hint.
    std::optional<bool> paddingLeft;

    /// Render padding after the hint.
    ///
    /// Note: Padding should use the editor's background color, not the
    /// background color of the hint itself. That means padding can be used
    /// to visually align/separate an inlay hint.
    std::optional<bool> paddingRight;

    /// A data entry field that is preserved on an inlay hint between
    /// a `textDocument/inlayHint` and a `inlayHint/resolve` request.
    std::optional<LSPAny> data;
};

struct InitializedParams {};

/// The result returned from an initialize request.
struct InitializeResult {
    /// The capabilities the language server provides.
    ServerCapabilities capabilities;

    /// Information about the server.
    ///
    /// @since 3.15.0
    std::optional<ServerInfo> serverInfo;
};

struct InitializeParams {
    /// @inherited from _InitializeParams
    /// The process Id of the parent process that started
    /// the server.
    ///
    /// Is `null` if the process has not been started by another process.
    /// If the parent process is not alive then the server should exit.
    std::optional<int> processId;

    /// @inherited from _InitializeParams
    /// Information about the client
    ///
    /// @since 3.15.0
    std::optional<ClientInfo> clientInfo;

    /// @inherited from _InitializeParams
    /// The locale the client is currently showing the user interface
    /// in. This must not necessarily be the locale of the operating
    /// system.
    ///
    /// Uses IETF language tags as the value's syntax
    /// (See https://en.wikipedia.org/wiki/IETF_language_tag)
    ///
    /// @since 3.16.0
    std::optional<std::string> locale;

    /// @inherited from _InitializeParams
    /// The rootPath of the workspace. Is null
    /// if no folder is open.
    ///
    /// @deprecated in favour of rootUri.
    std::optional<std::string> rootPath;

    /// @inherited from _InitializeParams
    /// The rootUri of the workspace. Is null if no
    /// folder is open. If both `rootPath` and `rootUri` are set
    /// `rootUri` wins.
    ///
    /// @deprecated in favour of workspaceFolders.
    std::optional<URI> rootUri;

    /// @inherited from _InitializeParams
    /// The capabilities provided by the client (editor or tool)
    ClientCapabilities capabilities;

    /// @inherited from _InitializeParams
    /// User provided initialization options.
    std::optional<LSPAny> initializationOptions;

    /// @inherited from _InitializeParams
    /// The initial trace setting. If omitted trace is disabled ('off').
    std::optional<TraceValue> trace;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;

    /// @inherited from WorkspaceFoldersInitializeParams
    /// The workspace folders configured in the client when the server starts.
    ///
    /// This property is only available if the client supports workspace folders.
    /// It can be `null` if the client supports workspace folders but none are
    /// configured.
    ///
    /// @since 3.6.0
    std::optional<std::vector<WorkspaceFolder>> workspaceFolders;
};

struct ImplementationParams {
    /// @inherited from TextDocumentPositionParams
    /// The text document.
    TextDocumentIdentifier textDocument;

    /// @inherited from TextDocumentPositionParams
    /// The position inside the text document.
    Position position;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;

    /// @mixin from PartialResultParams
    /// An optional token that a server can use to report partial results (e.g. streaming) to
    /// the client.
    std::optional<ProgressToken> partialResultToken;
};

/// Parameters for a {@link HoverRequest}.
struct HoverParams {
    /// @inherited from TextDocumentPositionParams
    /// The text document.
    TextDocumentIdentifier textDocument;

    /// @inherited from TextDocumentPositionParams
    /// The position inside the text document.
    Position position;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;
};

/// The result of a hover request.
struct Hover {
    /// The hover's content
    rfl::Variant<MarkupContent, MarkedString, std::vector<MarkedString>> contents;

    /// An optional range inside the text document that is used to
    /// visualize the hover, e.g. by changing the background color.
    std::optional<Range> range;
};

/// Parameters for a {@link FoldingRangeRequest}.
struct FoldingRangeParams {
    /// The text document.
    TextDocumentIdentifier textDocument;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;

    /// @mixin from PartialResultParams
    /// An optional token that a server can use to report partial results (e.g. streaming) to
    /// the client.
    std::optional<ProgressToken> partialResultToken;
};

/// Represents a folding range. To be valid, start and end line must be bigger than zero and smaller
/// than the number of lines in the document. Clients are free to ignore invalid ranges.
struct FoldingRange {
    /// The zero-based start line of the range to fold. The folded area starts after the line's last
    /// character. To be valid, the end must be zero or larger and smaller than the number of lines
    /// in the document.
    uint startLine;

    /// The zero-based character offset from where the folded range starts. If not defined, defaults
    /// to the length of the start line.
    std::optional<uint> startCharacter;

    /// The zero-based end line of the range to fold. The folded area ends with the line's last
    /// character. To be valid, the end must be zero or larger and smaller than the number of lines
    /// in the document.
    uint endLine;

    /// The zero-based character offset before the folded range ends. If not defined, defaults to
    /// the length of the end line.
    std::optional<uint> endCharacter;

    /// Describes the kind of the folding range such as 'comment' or 'region'. The kind
    /// is used to categorize folding ranges and used by commands like 'Fold all comments'.
    /// See {@link FoldingRangeKind} for an enumeration of standardized kinds.
    std::optional<FoldingRangeKind> kind;

    /// The text that the client should show when the specified range is
    /// collapsed. If not defined or not supported by the client, a default
    /// will be chosen by the client.
    ///
    /// @since 3.17.0
    std::optional<std::string> collapsedText;
};

struct FileSystemWatcher {
    /// The glob pattern to watch. See {@link GlobPattern glob pattern} for more detail.
    ///
    /// @since 3.17.0 support for relative patterns.
    GlobPattern globPattern;

    /// The kind of events of interest. If omitted it defaults
    /// to WatchKind.Create | WatchKind.Change | WatchKind.Delete
    /// which is 7.
    std::optional<WatchKind> kind;
};

/// The parameters of a {@link ExecuteCommandRequest}.
struct ExecuteCommandParams {
    /// The identifier of the actual command handler.
    std::string command;

    /// Arguments that the command should be invoked with.
    std::optional<std::vector<LSPAny>> arguments;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;
};

/// Parameters for a {@link DocumentSymbolRequest}.
struct DocumentSymbolParams {
    /// The text document.
    TextDocumentIdentifier textDocument;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;

    /// @mixin from PartialResultParams
    /// An optional token that a server can use to report partial results (e.g. streaming) to
    /// the client.
    std::optional<ProgressToken> partialResultToken;
};

/// Represents programming constructs like variables, classes, interfaces etc.
/// that appear in a document. Document symbols can be hierarchical and they
/// have two ranges: one that encloses its definition and one that points to
/// its most interesting range, e.g. the range of an identifier.
struct DocumentSymbol {
    /// The name of this symbol. Will be displayed in the user interface and therefore must not be
    /// an empty string or a string only consisting of white spaces.
    std::string name;

    /// More detail for this symbol, e.g the signature of a function.
    std::optional<std::string> detail;

    /// The kind of this symbol.
    SymbolKind kind;

    /// Tags for this document symbol.
    ///
    /// @since 3.16.0
    std::optional<std::vector<SymbolTag>> tags;

    /// Indicates if this symbol is deprecated.
    ///
    /// @deprecated Use tags instead
    std::optional<bool> deprecated;

    /// The range enclosing this symbol not including leading/trailing whitespace but everything
    /// else like comments. This information is typically used to determine if the clients cursor is
    /// inside the symbol to reveal in the symbol in the UI.
    Range range;

    /// The range that should be selected and revealed when this symbol is being picked, e.g the
    /// name of a function. Must be contained by the `range`.
    Range selectionRange;

    /// Children of this symbol, e.g. properties of a class.
    std::optional<std::vector<DocumentSymbol>> children;
};

/// The parameters of a {@link DocumentRangesFormattingRequest}.
///
/// @since 3.18.0
/// @proposed
struct DocumentRangesFormattingParams {
    /// The document to format.
    TextDocumentIdentifier textDocument;

    /// The ranges to format
    std::vector<Range> ranges;

    /// The format options
    FormattingOptions options;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;
};

/// The parameters of a {@link DocumentRangeFormattingRequest}.
struct DocumentRangeFormattingParams {
    /// The document to format.
    TextDocumentIdentifier textDocument;

    /// The range to format
    Range range;

    /// The format options
    FormattingOptions options;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;
};

/// The parameters of a {@link DocumentOnTypeFormattingRequest}.
struct DocumentOnTypeFormattingParams {
    /// The document to format.
    TextDocumentIdentifier textDocument;

    /// The position around which the on type formatting should happen.
    /// This is not necessarily the exact position where the character denoted
    /// by the property `ch` got typed.
    Position position;

    /// The character that has been typed that triggered the formatting
    /// on type request. That is not necessarily the last character that
    /// got inserted into the document since the client could auto insert
    /// characters as well (e.g. like automatic brace completion).
    std::string ch;

    /// The formatting options.
    FormattingOptions options;
};

/// The parameters of a {@link DocumentLinkRequest}.
struct DocumentLinkParams {
    /// The document to provide document links for.
    TextDocumentIdentifier textDocument;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;

    /// @mixin from PartialResultParams
    /// An optional token that a server can use to report partial results (e.g. streaming) to
    /// the client.
    std::optional<ProgressToken> partialResultToken;
};

/// A document link is a range in a text document that links to an internal or external resource,
/// like another text document or a web site.
struct DocumentLink {
    /// The range this link applies to.
    Range range;

    /// The uri this link points to. If missing a resolve request is sent later.
    std::optional<URI> target;

    /// The tooltip text when you hover over this link.
    ///
    /// If a tooltip is provided, is will be displayed in a string that includes instructions on how
    /// to trigger the link, such as `{0} (ctrl + click)`. The specific instructions vary depending
    /// on OS, user settings, and localization.
    ///
    /// @since 3.15.0
    std::optional<std::string> tooltip;

    /// A data entry field that is preserved on a document link between a
    /// DocumentLinkRequest and a DocumentLinkResolveRequest.
    std::optional<LSPAny> data;
};

/// Parameters for a {@link DocumentHighlightRequest}.
struct DocumentHighlightParams {
    /// @inherited from TextDocumentPositionParams
    /// The text document.
    TextDocumentIdentifier textDocument;

    /// @inherited from TextDocumentPositionParams
    /// The position inside the text document.
    Position position;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;

    /// @mixin from PartialResultParams
    /// An optional token that a server can use to report partial results (e.g. streaming) to
    /// the client.
    std::optional<ProgressToken> partialResultToken;
};

/// A document highlight is a range inside a text document which deserves
/// special attention. Usually a document highlight is visualized by changing
/// the background color of its range.
struct DocumentHighlight {
    /// The range this highlight applies to.
    Range range;

    /// The highlight kind, default is {@link DocumentHighlightKind.Text text}.
    std::optional<DocumentHighlightKind> kind;
};

/// The parameters of a {@link DocumentFormattingRequest}.
struct DocumentFormattingParams {
    /// The document to format.
    TextDocumentIdentifier textDocument;

    /// The format options.
    FormattingOptions options;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;
};

/// A partial result for a document diagnostic report.
///
/// @since 3.17.0
struct DocumentDiagnosticReportPartialResult {
    std::unordered_map<
        std::string, rfl::Variant<FullDocumentDiagnosticReport, UnchangedDocumentDiagnosticReport>>
        relatedDocuments;
};

/// The result of a document diagnostic pull request. A report can
/// either be a full report containing all diagnostics for the
/// requested document or an unchanged report indicating that nothing
/// has changed in terms of diagnostics in comparison to the last
/// pull request.
///
/// @since 3.17.0
using DocumentDiagnosticReport =
    rfl::Variant<RelatedFullDocumentDiagnosticReport, RelatedUnchangedDocumentDiagnosticReport>;

/// Parameters of the document diagnostic request.
///
/// @since 3.17.0
struct DocumentDiagnosticParams {
    /// The text document.
    TextDocumentIdentifier textDocument;

    /// The additional identifier  provided during registration.
    std::optional<std::string> identifier;

    /// The result id of a previous response if provided.
    std::optional<std::string> previousResultId;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;

    /// @mixin from PartialResultParams
    /// An optional token that a server can use to report partial results (e.g. streaming) to
    /// the client.
    std::optional<ProgressToken> partialResultToken;
};

/// Parameters for a {@link DocumentColorRequest}.
struct DocumentColorParams {
    /// The text document.
    TextDocumentIdentifier textDocument;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;

    /// @mixin from PartialResultParams
    /// An optional token that a server can use to report partial results (e.g. streaming) to
    /// the client.
    std::optional<ProgressToken> partialResultToken;
};

/// The parameters sent in a save text document notification
struct DidSaveTextDocumentParams {
    /// The document that was saved.
    TextDocumentIdentifier textDocument;

    /// Optional the content when saved. Depends on the includeText value
    /// when the save notification was requested.
    std::optional<std::string> text;
};

/// The params sent in a save notebook document notification.
///
/// @since 3.17.0
struct DidSaveNotebookDocumentParams {
    /// The notebook document that got saved.
    NotebookDocumentIdentifier notebookDocument;
};

/// The parameters sent in an open text document notification
struct DidOpenTextDocumentParams {
    /// The document that was opened.
    TextDocumentItem textDocument;
};

/// The params sent in an open notebook document notification.
///
/// @since 3.17.0
struct DidOpenNotebookDocumentParams {
    /// The notebook document that got opened.
    NotebookDocument notebookDocument;

    /// The text documents that represent the content
    /// of a notebook cell.
    std::vector<TextDocumentItem> cellTextDocuments;
};

/// The parameters sent in a close text document notification
struct DidCloseTextDocumentParams {
    /// The document that was closed.
    TextDocumentIdentifier textDocument;
};

/// The params sent in a close notebook document notification.
///
/// @since 3.17.0
struct DidCloseNotebookDocumentParams {
    /// The notebook document that got closed.
    NotebookDocumentIdentifier notebookDocument;

    /// The text documents that represent the content
    /// of a notebook cell that got closed.
    std::vector<TextDocumentIdentifier> cellTextDocuments;
};

/// The parameters of a `workspace/didChangeWorkspaceFolders` notification.
struct DidChangeWorkspaceFoldersParams {
    /// The actual workspace folder change event.
    WorkspaceFoldersChangeEvent event;
};

/// The watched files change notification's parameters.
struct DidChangeWatchedFilesParams {
    /// The actual file events.
    std::vector<FileEvent> changes;
};

/// The change text document notification's parameters.
struct DidChangeTextDocumentParams {
    /// The document that did change. The version number points
    /// to the version after all provided content changes have
    /// been applied.
    VersionedTextDocumentIdentifier textDocument;

    /// The actual content changes. The content changes describe single state changes
    /// to the document. So if there are two content changes c1 (at array index 0) and
    /// c2 (at array index 1) for a document in state S then c1 moves the document from
    /// S to S' and c2 from S' to S''. So c1 is computed on the state S and c2 is computed
    /// on the state S'.
    ///
    /// To mirror the content of a document using change events use the following approach:
    /// - start with the same initial content
    /// - apply the 'textDocument/didChange' notifications in the order you receive them.
    /// - apply the `TextDocumentContentChangeEvent`s in a single notification in the order
    ///   you receive them.
    std::vector<TextDocumentContentChangeEvent> contentChanges;
};

/// The params sent in a change notebook document notification.
///
/// @since 3.17.0
struct DidChangeNotebookDocumentParams {
    /// The notebook document that did change. The version number points
    /// to the version after all provided changes have been applied. If
    /// only the text document content of a cell changes the notebook version
    /// doesn't necessarily have to change.
    VersionedNotebookDocumentIdentifier notebookDocument;

    /// The actual changes to the notebook document.
    ///
    /// The changes describe single state changes to the notebook document.
    /// So if there are two changes c1 (at array index 0) and c2 (at array
    /// index 1) for a notebook in state S then c1 moves the notebook from
    /// S to S' and c2 from S' to S''. So c1 is computed on the state S and
    /// c2 is computed on the state S'.
    ///
    /// To mirror the content of a notebook using change events use the following approach:
    /// - start with the same initial content
    /// - apply the 'notebookDocument/didChange' notifications in the order you receive them.
    /// - apply the `NotebookChangeEvent`s in a single notification in the order
    ///   you receive them.
    NotebookDocumentChangeEvent change;
};

/// The parameters of a change configuration notification.
struct DidChangeConfigurationParams {
    /// The actual changed settings
    LSPAny settings;
};

/// The parameters sent in notifications/requests for user-initiated deletes of
/// files.
///
/// @since 3.16.0
struct DeleteFilesParams {
    /// An array of all files/folders deleted in this operation.
    std::vector<FileDelete> files;
};

/// Parameters for a {@link DefinitionRequest}.
struct DefinitionParams {
    /// @inherited from TextDocumentPositionParams
    /// The text document.
    TextDocumentIdentifier textDocument;

    /// @inherited from TextDocumentPositionParams
    /// The position inside the text document.
    Position position;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;

    /// @mixin from PartialResultParams
    /// An optional token that a server can use to report partial results (e.g. streaming) to
    /// the client.
    std::optional<ProgressToken> partialResultToken;
};

/// Information about where a symbol is defined.
///
/// Provides additional metadata over normal {@link Location location} definitions, including the
/// range of the defining symbol
using DefinitionLink = LocationLink;

/// The definition of a symbol represented as one or many {@link Location locations}.
/// For most programming languages there is only one location at which a symbol is
/// defined.
///
/// Servers should prefer returning `DefinitionLink` over `Definition` if supported
/// by the client.
using Definition = rfl::Variant<Location, std::vector<Location>>;

struct DeclarationParams {
    /// @inherited from TextDocumentPositionParams
    /// The text document.
    TextDocumentIdentifier textDocument;

    /// @inherited from TextDocumentPositionParams
    /// The position inside the text document.
    Position position;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;

    /// @mixin from PartialResultParams
    /// An optional token that a server can use to report partial results (e.g. streaming) to
    /// the client.
    std::optional<ProgressToken> partialResultToken;
};

/// Information about where a symbol is declared.
///
/// Provides additional metadata over normal {@link Location location} declarations, including the
/// range of the declaring symbol.
///
/// Servers should prefer returning `DeclarationLink` over `Declaration` if supported
/// by the client.
using DeclarationLink = LocationLink;

/// The declaration of a symbol representation as one or many {@link Location locations}.
using Declaration = rfl::Variant<Location, std::vector<Location>>;

/// The parameters sent in notifications/requests for user-initiated creation of
/// files.
///
/// @since 3.16.0
struct CreateFilesParams {
    /// An array of all files/folders created in this operation.
    std::vector<FileCreate> files;
};

/// The parameters of a configuration request.
struct ConfigurationParams {
    std::vector<ConfigurationItem> items;
};

/// Completion parameters
struct CompletionParams {
    /// The completion context. This is only available it the client specifies
    /// to send this using the client capability `textDocument.completion.contextSupport === true`
    std::optional<CompletionContext> context;

    /// @inherited from TextDocumentPositionParams
    /// The text document.
    TextDocumentIdentifier textDocument;

    /// @inherited from TextDocumentPositionParams
    /// The position inside the text document.
    Position position;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;

    /// @mixin from PartialResultParams
    /// An optional token that a server can use to report partial results (e.g. streaming) to
    /// the client.
    std::optional<ProgressToken> partialResultToken;
};

/// Represents a collection of {@link CompletionItem completion items} to be presented
/// in the editor.
struct CompletionList {
    /// This list it not complete. Further typing results in recomputing this list.
    ///
    /// Recomputed lists have all their items replaced (not appended) in the
    /// incomplete completion sessions.
    bool isIncomplete;

    /// In many cases the items of an actual completion result share the same
    /// value for properties like `commitCharacters` or the range of a text
    /// edit. A completion list can therefore define item defaults which will
    /// be used if a completion item itself doesn't specify the value.
    ///
    /// If a completion list specifies a default value and a completion item
    /// also specifies a corresponding value, the rules for combining these are
    /// defined by `applyKinds` (if the client supports it), defaulting to
    /// ApplyKind.Replace.
    ///
    /// Servers are only allowed to return default values if the client
    /// signals support for this via the `completionList.itemDefaults`
    /// capability.
    ///
    /// @since 3.17.0
    std::optional<CompletionItemDefaults> itemDefaults;

    /// Specifies how fields from a completion item should be combined with those
    /// from `completionList.itemDefaults`.
    ///
    /// If unspecified, all fields will be treated as ApplyKind.Replace.
    ///
    /// If a field's value is ApplyKind.Replace, the value from a completion item
    /// (if provided and not `null`) will always be used instead of the value
    /// from `completionItem.itemDefaults`.
    ///
    /// If a field's value is ApplyKind.Merge, the values will be merged using
    /// the rules defined against each field below.
    ///
    /// Servers are only allowed to return `applyKind` if the client
    /// signals support for this via the `completionList.applyKindSupport`
    /// capability.
    ///
    /// @since 3.18.0
    std::optional<CompletionItemApplyKinds> applyKind;

    /// The completion items.
    std::vector<CompletionItem> items;
};

/// Parameters for a {@link ColorPresentationRequest}.
struct ColorPresentationParams {
    /// The text document.
    TextDocumentIdentifier textDocument;

    /// The color to request presentations for.
    Color color;

    /// The range where the color would be inserted. Serves as a context.
    Range range;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;

    /// @mixin from PartialResultParams
    /// An optional token that a server can use to report partial results (e.g. streaming) to
    /// the client.
    std::optional<ProgressToken> partialResultToken;
};

struct ColorPresentation {
    /// The label of this color presentation. It will be shown on the color
    /// picker header. By default this is also the text that is inserted when selecting
    /// this color presentation.
    std::string label;

    /// An {@link TextEdit edit} which is applied to a document when selecting
    /// this presentation for the color.  When `falsy` the {@link ColorPresentation.label label}
    /// is used.
    std::optional<TextEdit> textEdit;

    /// An optional array of additional {@link TextEdit text edits} that are applied when
    /// selecting this color presentation. Edits must not overlap with the main {@link
    /// ColorPresentation.textEdit edit} nor with themselves.
    std::optional<std::vector<TextEdit>> additionalTextEdits;
};

/// Represents a color range from a document.
struct ColorInformation {
    /// The range in the document where this color appears.
    Range range;

    /// The actual color value for this color range.
    Color color;
};

/// The parameters of a {@link CodeLensRequest}.
struct CodeLensParams {
    /// The document to request code lens for.
    TextDocumentIdentifier textDocument;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;

    /// @mixin from PartialResultParams
    /// An optional token that a server can use to report partial results (e.g. streaming) to
    /// the client.
    std::optional<ProgressToken> partialResultToken;
};

/// A code lens represents a {@link Command command} that should be shown along with
/// source text, like the number of references, a way to run tests, etc.
///
/// A code lens is _unresolved_ when no command is associated to it. For performance
/// reasons the creation of a code lens and resolving should be done in two stages.
struct CodeLens {
    /// The range in which this code lens is valid. Should only span a single line.
    Range range;

    /// The command this code lens represents.
    std::optional<Command> command;

    /// A data entry field that is preserved on a code lens item between
    /// a {@link CodeLensRequest} and a {@link CodeLensResolveRequest}
    std::optional<LSPAny> data;
};

/// The parameters of a {@link CodeActionRequest}.
struct CodeActionParams {
    /// The document in which the command was invoked.
    TextDocumentIdentifier textDocument;

    /// The range for which the command was invoked.
    Range range;

    /// Context carrying additional information.
    CodeActionContext context;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;

    /// @mixin from PartialResultParams
    /// An optional token that a server can use to report partial results (e.g. streaming) to
    /// the client.
    std::optional<ProgressToken> partialResultToken;
};

/// A code action represents a change that can be performed in code, e.g. to fix a problem or
/// to refactor code.
///
/// A CodeAction must set either `edit` and/or a `command`. If both are supplied, the `edit` is
/// applied first, then the `command` is executed.
struct CodeAction {
    /// A short, human-readable, title for this code action.
    std::string title;

    /// The kind of the code action.
    ///
    /// Used to filter code actions.
    std::optional<CodeActionKind> kind;

    /// The diagnostics that this code action resolves.
    std::optional<std::vector<Diagnostic>> diagnostics;

    /// Marks this as a preferred action. Preferred actions are used by the `auto fix` command and
    /// can be targeted by keybindings.
    ///
    /// A quick fix should be marked preferred if it properly addresses the underlying error.
    /// A refactoring should be marked preferred if it is the most reasonable choice of actions to
    /// take.
    ///
    /// @since 3.15.0
    std::optional<bool> isPreferred;

    /// Marks that the code action cannot currently be applied.
    ///
    /// Clients should follow the following guidelines regarding disabled code actions:
    ///
    ///   - Disabled code actions are not shown in automatic
    ///   [lightbulbs](https://code.visualstudio.com/docs/editor/editingevolved#_code-action)
    ///     code action menus.
    ///
    ///   - Disabled actions are shown as faded out in the code action menu when the user requests a
    ///   more specific type
    ///     of code action, such as refactorings.
    ///
    ///   - If the user has a
    ///   [keybinding](https://code.visualstudio.com/docs/editor/refactoring#_keybindings-for-code-actions)
    ///     that auto applies a code action and only disabled code actions are returned, the client
    ///     should show the user an error message with `reason` in the editor.
    ///
    /// @since 3.16.0
    std::optional<CodeActionDisabled> disabled;

    /// The workspace edit this code action performs.
    std::optional<WorkspaceEdit> edit;

    /// A command this code action executes. If a code action
    /// provides an edit and a command, first the edit is
    /// executed and then the command.
    std::optional<Command> command;

    /// A data entry field that is preserved on a code action between
    /// a `textDocument/codeAction` and a `codeAction/resolve` request.
    ///
    /// @since 3.16.0
    std::optional<LSPAny> data;

    /// Tags for this code action.
    ///
    /// @since 3.18.0 - proposed
    std::optional<std::vector<CodeActionTag>> tags;
};

struct CancelParams {
    /// The request id to cancel.
    rfl::Variant<int, std::string> id;
};

/// The parameter of a `textDocument/prepareCallHierarchy` request.
///
/// @since 3.16.0
struct CallHierarchyPrepareParams {
    /// @inherited from TextDocumentPositionParams
    /// The text document.
    TextDocumentIdentifier textDocument;

    /// @inherited from TextDocumentPositionParams
    /// The position inside the text document.
    Position position;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;
};

/// The parameter of a `callHierarchy/outgoingCalls` request.
///
/// @since 3.16.0
struct CallHierarchyOutgoingCallsParams {
    CallHierarchyItem item;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;

    /// @mixin from PartialResultParams
    /// An optional token that a server can use to report partial results (e.g. streaming) to
    /// the client.
    std::optional<ProgressToken> partialResultToken;
};

/// Represents an outgoing call, e.g. calling a getter from a method or a method from a constructor
/// etc.
///
/// @since 3.16.0
struct CallHierarchyOutgoingCall {
    /// The item that is called.
    CallHierarchyItem to;

    /// The range at which this item is called. This is the range relative to the caller, e.g the
    /// item passed to {@link CallHierarchyItemProvider.provideCallHierarchyOutgoingCalls
    /// `provideCallHierarchyOutgoingCalls`} and not {@link CallHierarchyOutgoingCall.to `this.to`}.
    std::vector<Range> fromRanges;
};

/// The parameter of a `callHierarchy/incomingCalls` request.
///
/// @since 3.16.0
struct CallHierarchyIncomingCallsParams {
    CallHierarchyItem item;

    /// @mixin from WorkDoneProgressParams
    /// An optional token that a server can use to report work done progress.
    std::optional<ProgressToken> workDoneToken;

    /// @mixin from PartialResultParams
    /// An optional token that a server can use to report partial results (e.g. streaming) to
    /// the client.
    std::optional<ProgressToken> partialResultToken;
};

/// Represents an incoming call, e.g. a caller of a method or constructor.
///
/// @since 3.16.0
struct CallHierarchyIncomingCall {
    /// The item that makes the call.
    CallHierarchyItem from;

    /// The ranges at which the calls appear. This is relative to the caller
    /// denoted by {@link CallHierarchyIncomingCall.from `this.from`}.
    std::vector<Range> fromRanges;
};

/// The result returned from the apply workspace edit request.
///
/// @since 3.17 renamed from ApplyWorkspaceEditResponse
struct ApplyWorkspaceEditResult {
    /// Indicates whether the edit was applied or not.
    bool applied;

    /// An optional textual description for why the edit was not applied.
    /// This may be used by the server for diagnostic logging or to provide
    /// a suitable error for a request that triggered the edit.
    std::optional<std::string> failureReason;

    /// Depending on the client's failure handling strategy `failedChange` might
    /// contain the index of the change that failed. This property is only available
    /// if the client signals a `failureHandlingStrategy` in its client capabilities.
    std::optional<uint> failedChange;
};

/// The parameters passed via an apply workspace edit request.
struct ApplyWorkspaceEditParams {
    /// An optional label of the workspace edit. This label is
    /// presented in the user interface for example on an undo
    /// stack to undo the workspace edit.
    std::optional<std::string> label;

    /// The edits to apply.
    WorkspaceEdit edit;

    /// Additional data about the edit.
    ///
    /// @since 3.18.0
    /// @proposed
    std::optional<WorkspaceEditMetadata> metadata;
};

/// Registration options for a {@link WorkspaceSymbolRequest}.
struct WorkspaceSymbolRegistrationOptions {
    /// @inherited from WorkspaceSymbolOptions
    /// The server provides support to resolve additional
    /// information for a workspace symbol.
    ///
    /// @since 3.17.0
    std::optional<bool> resolveProvider;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

struct WorkDoneProgressReport {
    rfl::Literal<"report"> kind;

    /// Controls enablement state of a cancel button.
    ///
    /// Clients that don't support cancellation or don't support controlling the button's
    /// enablement state are allowed to ignore the property.
    std::optional<bool> cancellable;

    /// Optional, more detailed associated progress message. Contains
    /// complementary information to the `title`.
    ///
    /// Examples: "3/25 files", "project/src/module2", "node_modules/some_dep".
    /// If unset, the previous progress message (if any) is still valid.
    std::optional<std::string> message;

    /// Optional progress percentage to display (value 100 is considered 100%).
    /// If not provided infinite progress is assumed and clients are allowed
    /// to ignore the `percentage` value in subsequent in report notifications.
    ///
    /// The value should be steadily rising. Clients are free to ignore values
    /// that are not following this rule. The value range is [0, 100]
    std::optional<uint> percentage;
};

struct WorkDoneProgressEnd {
    rfl::Literal<"end"> kind;

    /// Optional, a final message indicating to for example indicate the outcome
    /// of the operation.
    std::optional<std::string> message;
};

struct WorkDoneProgressBegin {
    rfl::Literal<"begin"> kind;

    /// Mandatory title of the progress operation. Used to briefly inform about
    /// the kind of operation being performed.
    ///
    /// Examples: "Indexing" or "Linking dependencies".
    std::string title;

    /// Controls if a cancel button should show to allow the user to cancel the
    /// long running operation. Clients that don't support cancellation are allowed
    /// to ignore the setting.
    std::optional<bool> cancellable;

    /// Optional, more detailed associated progress message. Contains
    /// complementary information to the `title`.
    ///
    /// Examples: "3/25 files", "project/src/module2", "node_modules/some_dep".
    /// If unset, the previous progress message (if any) is still valid.
    std::optional<std::string> message;

    /// Optional progress percentage to display (value 100 is considered 100%).
    /// If not provided infinite progress is assumed and clients are allowed
    /// to ignore the `percentage` value in subsequent in report notifications.
    ///
    /// The value should be steadily rising. Clients are free to ignore values
    /// that are not following this rule. The value range is [0, 100].
    std::optional<uint> percentage;
};

/// Save registration options.
struct TextDocumentSaveRegistrationOptions {
    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @inherited from SaveOptions
    /// The client is supposed to include the content on save.
    std::optional<bool> includeText;
};

/// Describe options to be used when registered for text document change events.
struct TextDocumentChangeRegistrationOptions {
    /// How documents are synced to the server.
    TextDocumentSyncKind syncKind;

    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;
};

/// Registration options for a {@link SignatureHelpRequest}.
struct SignatureHelpRegistrationOptions {
    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @inherited from SignatureHelpOptions
    /// List of characters that trigger signature help automatically.
    std::optional<std::vector<std::string>> triggerCharacters;

    /// @inherited from SignatureHelpOptions
    /// List of characters that re-trigger signature help.
    ///
    /// These trigger characters are only active when signature help is already showing. All trigger
    /// characters are also counted as re-trigger characters.
    ///
    /// @since 3.15.0
    std::optional<std::vector<std::string>> retriggerCharacters;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// A set of predefined token types. This set is not fixed
/// an clients can specify additional token types via the
/// corresponding client capabilities.
///
/// @since 3.16.0
using SemanticTokenTypes =
    rfl::Literal<"namespace", "type", "class", "enum", "interface", "struct", "typeParameter",
                 "parameter", "variable", "property", "enumMember", "event", "function", "method",
                 "macro", "keyword", "modifier", "comment", "string", "number", "regexp",
                 "operator", "decorator", "label">;

/// A set of predefined token modifiers. This set is not fixed
/// an clients can specify additional token types via the
/// corresponding client capabilities.
///
/// @since 3.16.0
using SemanticTokenModifiers =
    rfl::Literal<"declaration", "definition", "readonly", "static", "deprecated", "abstract",
                 "async", "modification", "documentation", "defaultLibrary">;

/// Registration options for a {@link RenameRequest}.
struct RenameRegistrationOptions {
    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @inherited from RenameOptions
    /// Renames should be checked and tested before being executed.
    ///
    /// @since version 3.12.0
    std::optional<bool> prepareProvider;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Registration options for a {@link ReferencesRequest}.
struct ReferenceRegistrationOptions {
    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

enum class LSPErrorCodes : int32_t {
    RequestFailed = -32803,
    ServerCancelled = -32802,
    ContentModified = -32801,
    RequestCancelled = -32800,
};

/// Inline completion options used during static or dynamic registration.
///
/// @since 3.18.0
/// @proposed
struct InlineCompletionRegistrationOptions {
    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;

    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @mixin from StaticRegistrationOptions
    /// The id used to register the request. The id can be used to deregister
    /// the request again. See also Registration#id.
    std::optional<std::string> id;
};

/// The data type of the ResponseError if the
/// initialize request fails.
struct InitializeError {
    /// Indicates whether the client execute the following retry logic:
    /// (1) show the message provided by the ResponseError to the user
    /// (2) user selects retry or cancel
    /// (3) if user selected retry the initialize method is sent again.
    bool retry;
};

/// Registration options for a {@link HoverRequest}.
struct HoverRegistrationOptions {
    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Registration options for a {@link ExecuteCommandRequest}.
struct ExecuteCommandRegistrationOptions {
    /// @inherited from ExecuteCommandOptions
    /// The commands to be executed on the server
    std::vector<std::string> commands;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Predefined error codes.
enum class ErrorCodes : int32_t {
    ParseError = -32700,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams = -32602,
    InternalError = -32603,
    ServerNotInitialized = -32002,
    UnknownErrorCode = -32001,
};

/// Registration options for a {@link DocumentSymbolRequest}.
struct DocumentSymbolRegistrationOptions {
    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @inherited from DocumentSymbolOptions
    /// A human-readable string that is shown when multiple outlines trees
    /// are shown for the same document.
    ///
    /// @since 3.16.0
    std::optional<std::string> label;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Registration options for a {@link DocumentRangeFormattingRequest}.
struct DocumentRangeFormattingRegistrationOptions {
    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @inherited from DocumentRangeFormattingOptions
    /// Whether the server supports formatting multiple ranges at once.
    ///
    /// @since 3.18.0
    /// @proposed
    std::optional<bool> rangesSupport;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Registration options for a {@link DocumentOnTypeFormattingRequest}.
struct DocumentOnTypeFormattingRegistrationOptions {
    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @inherited from DocumentOnTypeFormattingOptions
    /// A character on which formatting should be triggered, like `{`.
    std::string firstTriggerCharacter;

    /// @inherited from DocumentOnTypeFormattingOptions
    /// More trigger characters.
    std::optional<std::vector<std::string>> moreTriggerCharacter;
};

/// Registration options for a {@link DocumentLinkRequest}.
struct DocumentLinkRegistrationOptions {
    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @inherited from DocumentLinkOptions
    /// Document links have a resolve provider as well.
    std::optional<bool> resolveProvider;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Registration options for a {@link DocumentHighlightRequest}.
struct DocumentHighlightRegistrationOptions {
    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Registration options for a {@link DocumentFormattingRequest}.
struct DocumentFormattingRegistrationOptions {
    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// The document diagnostic report kinds.
///
/// @since 3.17.0
using DocumentDiagnosticReportKind = rfl::Literal<"full", "unchanged">;

/// Describe options to be used when registered for text document change events.
struct DidChangeWatchedFilesRegistrationOptions {
    /// The watchers to register.
    std::vector<FileSystemWatcher> watchers;
};

struct DidChangeConfigurationRegistrationOptions {
    std::optional<rfl::Variant<std::string, std::vector<std::string>>> section;
};

/// Cancellation data returned from a diagnostic request.
///
/// @since 3.17.0
struct DiagnosticServerCancellationData {
    bool retriggerRequest;
};

/// Registration options for a {@link DefinitionRequest}.
struct DefinitionRegistrationOptions {
    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Registration options for a {@link CompletionRequest}.
struct CompletionRegistrationOptions {
    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @inherited from CompletionOptions
    /// Most tools trigger completion request automatically without explicitly requesting
    /// it using a keyboard shortcut (e.g. Ctrl+Space). Typically they do so when the user
    /// starts to type an identifier. For example if the user types `c` in a JavaScript file
    /// code complete will automatically pop up present `console` besides others as a
    /// completion item. Characters that make up identifiers don't need to be listed here.
    ///
    /// If code complete should automatically be trigger on characters not being valid inside
    /// an identifier (for example `.` in JavaScript) list them in `triggerCharacters`.
    std::optional<std::vector<std::string>> triggerCharacters;

    /// @inherited from CompletionOptions
    /// The list of all possible characters that commit a completion. This field can be used
    /// if clients don't support individual commit characters per completion item. See
    /// `ClientCapabilities.textDocument.completion.completionItem.commitCharactersSupport`
    ///
    /// If a server provides both `allCommitCharacters` and commit characters on an individual
    /// completion item the ones on the completion item win.
    ///
    /// @since 3.2.0
    std::optional<std::vector<std::string>> allCommitCharacters;

    /// @inherited from CompletionOptions
    /// The server provides support to resolve additional
    /// information for a completion item.
    std::optional<bool> resolveProvider;

    /// @inherited from CompletionOptions
    /// The server supports the following `CompletionItem` specific
    /// capabilities.
    ///
    /// @since 3.17.0
    std::optional<ServerCompletionItemOptions> completionItem;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Registration options for a {@link CodeLensRequest}.
struct CodeLensRegistrationOptions {
    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @inherited from CodeLensOptions
    /// Code lens has a resolve provider as well.
    std::optional<bool> resolveProvider;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

/// Registration options for a {@link CodeActionRequest}.
struct CodeActionRegistrationOptions {
    /// @inherited from TextDocumentRegistrationOptions
    /// A document selector to identify the scope of the registration. If set to null
    /// the document selector provided on the client side will be used.
    std::optional<DocumentSelector> documentSelector;

    /// @inherited from CodeActionOptions
    /// CodeActionKinds that this server may return.
    ///
    /// The list of kinds may be generic, such as `CodeActionKind.Refactor`, or the server
    /// may list out every specific kind they provide.
    std::optional<std::vector<CodeActionKind>> codeActionKinds;

    /// @inherited from CodeActionOptions
    /// Static documentation for a class of code actions.
    ///
    /// Documentation from the provider should be shown in the code actions menu if either:
    ///
    /// - Code actions of `kind` are requested by the editor. In this case, the editor will show the
    /// documentation that
    ///   most closely matches the requested code action kind. For example, if a provider has
    ///   documentation for both `Refactor` and `RefactorExtract`, when the user requests code
    ///   actions for `RefactorExtract`, the editor will use the documentation for `RefactorExtract`
    ///   instead of the documentation for `Refactor`.
    ///
    /// - Any code actions of `kind` are returned by the provider.
    ///
    /// At most one documentation entry should be shown per provider.
    ///
    /// @since 3.18.0
    /// @proposed
    std::optional<std::vector<CodeActionKindDocumentation>> documentation;

    /// @inherited from CodeActionOptions
    /// The server provides support to resolve additional
    /// information for a code action.
    ///
    /// @since 3.16.0
    std::optional<bool> resolveProvider;

    /// @mixin from WorkDoneProgressOptions
    std::optional<bool> workDoneProgress;
};

} // namespace lsp
