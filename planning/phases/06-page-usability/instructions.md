# Phase 06 — Page-level usability

## Objective

Make every page easier to scan and operate by keeping the primary task visible and placing secondary setup or advanced controls behind clear, keyboard-accessible disclosure controls.

## Approved requirements and acceptance criteria

- Apply the usability pass across all application pages, not only Storage and Updates.
- Keep each page's main action and current work visible; collapse optional setup, advanced tools, and secondary details.
- Keep existing route indices, service calls, control behavior, and persisted data unchanged.
- Keep the existing native Win32/WTL style, theme, and C++20 modular architecture.
- Make disclosure controls focusable and operable with standard button keyboard behavior.
- Do not add third-party UI dependencies or replace the native controls.

## Design direction

Apply the relevant UI/UX Pro Max guidance for task hierarchy, progressive disclosure, concise labels, and keyboard access. This is a native desktop management application, so use existing WTL controls and the established GTH theme rather than web-specific components or marketing layouts.

## Verification

- Build the Debug app target.
- Launch the Debug app for owner review without replacing or closing a running Release instance.
- Record that automated tests were not run for this layout-only pass.
