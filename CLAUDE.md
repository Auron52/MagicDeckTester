# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Purpose

MagicDeckTester simulates Magic: The Gathering games to compare card and deck performance. The goal is to let users build or modify decks and run simulated games to evaluate how different card choices affect outcomes.

## Project Status

The application is in early development — no build system, test runner, or source files exist yet. When commands are established, update this file with how to build, run, and test the project.

## MTG Rules Skill

This project has a custom skill at `.claude/skills/mtg-rules.md` that **all agents working in this repository must use**. It is the authoritative reference for both MTG rules correctness and implementation patterns.

The skill has four modes:

| Invocation | Purpose |
|------------|---------|
| `/mtg-rules <question>` | Answer a rules question with Comprehensive Rules citations |
| `/mtg-rules review` | Review code or a diff for rule-violation bugs |
| `/mtg-rules build <topic>` | Get data models and implementation patterns for an MTG concept |
| `/mtg-rules card <oracle text>` | Generate an implementation stub from card oracle text |
| `/mtg-rules card <oracle text> review <code>` | Review code against a specific card's oracle text as the spec |

### When to invoke the skill

- **Before implementing any MTG game mechanic** — use `build` mode to get correct data models and patterns. Do not implement from memory; the skill contains the ground-truth rules.
- **Before implementing any specific card** — use `card` mode with the card's oracle text to generate the correct ability type, timing, and targeting structure.
- **After implementing any MTG logic** — use `review` mode to catch rule-violation bugs before committing.
- **When a rules question arises during development** — use rules mode rather than relying on training data; edge cases (layer system, state-based actions, replacement effects) are subtle and the skill has the precise rules.

### Key correctness areas the skill covers

The skill contains detailed rules and implementation guidance for the areas most commonly implemented incorrectly:

- Stack resolution and priority passing
- State-based actions (must run after every event, not just end of turn)
- Combat damage assignment including trample + deathtouch interactions
- Summoning sickness tracking across turns
- Zone transitions (objects become new objects when changing zones)
- The layer system for continuous effects
- Triggered vs. replacement effects (replacement effects do not use the stack)
