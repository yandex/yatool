# Autocheck blacklist matcher

The matcher accepts one normalized relative directory rule per line. Text after `#` is a comment.

- `path` marks the directory and its descendants as blacklisted.
- `!path` marks the directory and its descendants as not blacklisted.
- Matching is performed on complete slash-separated components.
- The deepest matching explicit rule determines the result.
- Empty path components and `.` are removed during lexical normalization; `..` removes the preceding component. Absolute, empty, and root-escaping paths are invalid.
- Repeating a normalized path with the same status in one file emits a warning and keeps that status.
- Declaring both `path` and `!path` for the same normalized path in one file is an error; none of that file's rules are applied.
- When several blacklist files are loaded in order, a repeated path emits a warning and the declaration from the later file wins.

Redefinition diagnostics include the normalized path and the file and line of both declarations. Normalization aliases such as `market//front`, `market/./front`, and `market/legacy/../front` therefore count as declarations of the same path.

This deliberately does not implement the complete `.gitignore` language. There are no globs or escaping rules, and declaration order does not override path specificity. For example, with `devtools` and `!devtools/ya`, `devtools/ya/bin` is not blacklisted even if the broader `devtools` rule appears later.

## Performance benchmark

`TSvnBlacklist` uses the component Trie matcher unconditionally. The benchmark at `devtools/common/blacklist/benchmark` reads the versioned `build/rules/autocheck.blacklist` input and measures parsing, Trie construction, lookup, traversal counters, and retained-memory estimates separately.
