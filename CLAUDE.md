# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Purpose

MagicDeckTester simulates Magic: The Gathering games to compare card and deck performance. The goal is to let users build or modify decks and run simulated games to evaluate how different card choices affect outcomes.

## Project Status

The application is in early development — no build system, test runner, or source files exist yet. When commands are established, update this file with how to build, run, and test the project.

## MTG Rules Skill

This project has a custom skill at `.claude/skills/mtg-rules.md` that **all agents working in this repository must use**. It is the authoritative reference for both MTG rules correctness and implementation patterns.

The skill is not an invokable slash command. Access it by reading the file directly:

```
Read `.claude/skills/mtg-rules.md` and [answer / implement / review] ...
```

It covers four modes of use:

| Mode | Example prompt |
|------|---------------|
| Rules question | "Read the skill and answer: how does the legend rule work?" |
| Code review | "Read the skill and review this implementation for rule violations." |
| Build guidance | "Read the skill and give me patterns for implementing the stack." |
| Card implementation | "Read the skill and implement this card: [oracle text]" |

### When to consult the skill

- **Before implementing any MTG game mechanic** — read the skill for correct data models and patterns; do not implement from memory.
- **Before implementing any specific card** — read the skill and provide the card's oracle text to get the correct ability type, timing, and targeting structure.
- **After implementing any MTG logic** — read the skill and review the code for rule-violation bugs before committing.
- **When a rules question arises during development** — read the skill rather than relying on training data; edge cases (layer system, state-based actions, replacement effects) are subtle.

### Key correctness areas the skill covers

The skill contains detailed rules and implementation guidance for the areas most commonly implemented incorrectly:

- Stack resolution and priority passing
- State-based actions (must run after every event, not just end of turn)
- Combat damage assignment including trample + deathtouch interactions
- Summoning sickness tracking across turns
- Zone transitions (objects become new objects when changing zones)
- The layer system for continuous effects
- Triggered vs. replacement effects (replacement effects do not use the stack)
