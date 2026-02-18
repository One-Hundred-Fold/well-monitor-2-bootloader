# Cursor Rules for well-monitor-2-bootloader

This directory contains Cursor Rules files (`.mdc` format) that guide AI assistants working on this STM32Cube project.

## Rule Files

### `stm32cube-user-code-blocks.mdc`
- **Type**: Always applied
- **Purpose**: Enforces modification of STM32Cube-generated files only within USER CODE blocks
- **Critical**: Prevents code loss during project regeneration

### `stm32cube-ioc-file-modifications.mdc`
- **Type**: Always applied
- **Purpose**: Governs how to handle `.ioc` file modifications for peripheral configuration
- **Critical**: Ensures proper coordination between hardware configuration and code changes

## How Rules Work

1. **Always Applied**: Rules with `alwaysApply: true` are shown to AI on every request
2. **File-based**: Rules with `globs` are applied when editing matching files
3. **Manual**: Rules with only `description` can be fetched when relevant

## Creating New Rules

1. Create a `.mdc` file in this directory
2. Add frontmatter with metadata:
   ```markdown
   ---
   alwaysApply: true  # or false
   description: "Description of when to use this rule"
   globs: *.c,*.h     # file patterns (optional)
   ---
   ```
3. Write rules in Markdown format
4. Reference files using: `[filename](mdc:path/to/file.ext)`

## Best Practices

- Keep rules focused and specific
- Use file references to help AI navigate codebase
- Document both what to do (prescriptions) and what not to do (proscriptions)
- Update rules as project evolves
