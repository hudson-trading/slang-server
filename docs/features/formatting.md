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
        "timeout": 5000,
    },
]
```

`timeout` is how many milliseconds to wait before stopping the formatter (default `2000`; `0` disables the limit). The formatter runs in the background, so VSCode stays responsive. If it takes longer than a second, a notification appears with a Cancel button. Typing during a format also cancels it.

To have files in these folders then format on save, add the following to your user settings:
```json
"[systemverilog]": {
    "editor.formatOnSave": true
}
```
