# Project Instructions

# Communication

- Be brief in comments, commit messages, and replies.
- Avoid praise, filler, and agreement theater. State facts.
- Prefer direct answers over caveats.

# Language preference

- Use C++ for application code by default.
- Use C when a C ABI, system API, or lower-level boundary makes it simpler.
- Keep C and C++ interfaces clearly separated.

# Code style

- Keep names short and clear.
- Avoid magic values. Use named constants or enums.
- Keep self-explanatory one-off values inline.
- Prefer early returns and `continue` over deep nesting.
- Always use `{}` for conditionals where the language supports it.
- Add blank lines between logical blocks.
- Comment intent, constraints, or design. Do not restate code.

# Design

- Keep scope small. Do not touch unrelated code.
- Keep fields and functions private unless external access is required.
- Ask before widening visibility.
- Encapsulate low-level mechanics behind clear APIs.
- Respect layer boundaries. Call only the next lower layer.
- Use enums instead of boolean parameters when the value affects behavior.

# Bugs

- For bug fixes: write a failing test first.
- Observe the failure.
- Fix the bug.
- Verify the test passes.

# Validation

- Run the smallest relevant formatter, linter, or test.
- If validation is skipped, say why.

# Handoff

- For long tasks, major pivots, or low context, suggest `/handoff <next task>`.
- Use handoff to continue in a fresh session with only relevant context.
- Keep the handoff goal specific and action-oriented.

# Commit messages

- Separate subject and body with one blank line.
- Keep the subject under 50 characters.
- Capitalize the subject.
- Do not end the subject with a period.
- Use imperative mood.
- Wrap the body at 72 columns.
- Explain what and why, not how.
