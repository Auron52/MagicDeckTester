#!/usr/bin/env python3
"""Set container-local Codex access defaults, preserving other user settings."""
import os
import tomllib
from pathlib import Path

if not (Path('/.dockerenv').exists() or Path('/run/.containerenv').exists()):
    raise SystemExit('This setup is intended to run inside a container only.')
config = Path(os.environ.get('CODEX_HOME', str(Path.home() / '.codex'))) / 'config.toml'
config.parent.mkdir(parents=True, exist_ok=True)
original = config.read_text() if config.exists() else ''
settings = tomllib.loads(original)  # Refuse to modify an invalid existing config.
defaults = {'approval_policy': 'never', 'sandbox_mode': 'danger-full-access'}
missing = {key: value for key, value in defaults.items() if key not in settings}
prefix = ''.join(f'{key} = "{value}"\n' for key, value in missing.items())
updated = prefix + ('\n' if prefix and original else '') + original
tomllib.loads(updated)
if updated != original:
    if original:
        backup = config.with_name('config.toml.before-container-defaults')
        if not backup.exists():
            backup.write_text(original)
            backup.chmod(0o600)
    temporary = config.with_name('config.toml.container-tmp')
    temporary.write_text(updated)
    temporary.chmod(0o600)
    temporary.replace(config)
print(f'Container Codex access defaults checked in {config}')
