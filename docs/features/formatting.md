# Formatting

`slang-server` will eventually ship with its own formatter. Until then, VSCode users can configure external formatters via the vscode settings.

For Example:
```json
"slang.formatters": [
    {
        "command": "/path/to/formatter --flagfile=path/to/flags.txt --column_limit 100 -",
        "dirs": [
            "path/to/project1",
            "path/to/project2",
        ],
        "languageIds": ["systemverilog"],
    },
]
```

To have files in these folders then format on save, add the following to your user settings:
```json
"[systemverilog]": {
    "editor.formatOnSave": true
}
```
