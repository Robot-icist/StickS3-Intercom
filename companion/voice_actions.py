"""Route explicitly prefixed final transcripts to local agent harnesses."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


@dataclass(frozen=True)
class Intent:
    target: str
    content: str


_PREFIXES = (
    ("run action", "action"),
    ("open code", "opencode"),
    ("opencode", "opencode"),
    ("claude code", "claude"),
    ("claude", "claude"),
    ("github copilot", "copilot"),
    ("copilot", "copilot"),
    ("dictate", "dictation"),
)


def parse_intent(transcript: str) -> Intent:
    """Parse a spoken target prefix; unprefixed speech remains dictation."""
    stripped = transcript.strip()
    lowered = stripped.casefold()
    for prefix, target in _PREFIXES:
        if lowered == prefix:
            return Intent("pending", "")
        if lowered.startswith(prefix) and len(lowered) > len(prefix):
            boundary = stripped[len(prefix)]
            if boundary in (":", " ", ","):
                return Intent(target, stripped[len(prefix) + 1 :].strip())
    return Intent("dictation", stripped)


def default_actions_path() -> Path:
    root = os.environ.get("LOCALAPPDATA") or os.environ.get("XDG_CONFIG_HOME")
    base = Path(root) if root else Path.home() / ".config"
    return base / "StickS3Intercom" / "actions.json"


@dataclass(frozen=True)
class Action:
    command: tuple[str, ...]
    cwd: Optional[Path]
    description: str


def load_actions(path: Path) -> dict[str, Action]:
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return {}
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot load actions configuration {path}: {error}") from error

    definitions = raw.get("actions") if isinstance(raw, dict) else None
    if not isinstance(definitions, dict):
        raise ValueError(f"{path} must contain an 'actions' object")

    actions: dict[str, Action] = {}
    for name, definition in definitions.items():
        if not isinstance(name, str) or not name.strip():
            raise ValueError(f"{path} contains an empty action name")
        if not isinstance(definition, dict):
            raise ValueError(f"action {name!r} must be an object")
        command = definition.get("command")
        if (not isinstance(command, list) or not command or
                any(not isinstance(part, str) or not part for part in command)):
            raise ValueError(
                f"action {name!r} needs a non-empty string-array 'command'"
            )
        cwd_value = definition.get("cwd")
        if cwd_value is not None and not isinstance(cwd_value, str):
            raise ValueError(f"action {name!r} has a non-string cwd")
        cwd = Path(cwd_value) if cwd_value else None
        if cwd is not None and not cwd.is_absolute():
            cwd = path.parent / cwd
        description = definition.get("description", "")
        if not isinstance(description, str):
            raise ValueError(f"action {name!r} has a non-string description")
        actions[name.casefold()] = Action(tuple(command), cwd, description)
    return actions


class VoiceCommandRouter:
    def __init__(
        self,
        *,
        opencode_command: str,
        opencode_session: Optional[str],
        claude_command: str,
        claude_session: Optional[str],
        claude_cwd: Optional[Path],
        copilot_command: str,
        copilot_cwd: Optional[Path],
        actions_path: Path,
        timeout: float = 300,
    ) -> None:
        self.opencode_command = opencode_command
        self.opencode_session = opencode_session
        self.opencode_cwd: Optional[Path] = None
        self.claude_command = claude_command
        self.claude_session = claude_session
        self.claude_cwd = claude_cwd
        self.copilot_command = copilot_command
        self.copilot_cwd = copilot_cwd
        self.timeout = timeout
        self.actions = load_actions(actions_path)

    def handle(self, transcript: str) -> Optional[str]:
        """Return dictation text, or route a prefixed command and return None."""
        intent = parse_intent(transcript)
        if intent.target == "dictation":
            return intent.content
        if intent.target == "pending":
            print("Voice command prefix had no content; nothing was sent.")
            return None

        if not intent.content:
            print("Voice command was empty; nothing was sent.")
            return None
        if intent.target == "opencode":
            self._send_to_opencode(intent.content)
        elif intent.target == "claude":
            self._send_to_claude(intent.content)
        elif intent.target == "copilot":
            self._send_to_copilot(intent.content)
        elif intent.target == "action":
            self._run_action(intent.content)
        return None

    @staticmethod
    def _confirm(target: str, content: str) -> bool:
        print(f"\nVoice command target: {target}\n  {content}")
        try:
            answer = input("Confirm? [y/N] ").strip().casefold()
        except (EOFError, OSError):
            print("No confirmation input; command cancelled.")
            return False
        if answer not in ("y", "yes"):
            print("Command cancelled.")
            return False
        return True

    def _opencode_cli(self) -> str:
        executable = shutil.which(self.opencode_command)
        if executable is None:
            raise RuntimeError(
                f"OpenCode command {self.opencode_command!r} was not found on PATH"
            )
        return executable

    def _choose_opencode_session(self) -> str:
        if self.opencode_session:
            return self.opencode_session

        executable = self._opencode_cli()
        try:
            result = subprocess.run(
                [executable, "session", "list", "--format", "json"],
                cwd=Path.cwd(),
                capture_output=True,
                text=True,
                timeout=30,
                check=False,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise RuntimeError(f"Could not list OpenCode sessions: {error}") from error
        if result.returncode != 0:
            detail = result.stderr.strip() or result.stdout.strip()
            raise RuntimeError(
                f"OpenCode session listing failed with exit code "
                f"{result.returncode}: {detail}"
            )
        try:
            sessions = json.loads(result.stdout)
        except json.JSONDecodeError as error:
            raise RuntimeError("OpenCode returned an invalid session list") from error
        if not isinstance(sessions, list):
            raise RuntimeError("OpenCode returned an invalid session list")
        if not sessions:
            raise RuntimeError(
                "OpenCode has no sessions; open a project/session first"
            )
        print("\nOpenCode sessions:")
        for index, session in enumerate(sessions, start=1):
            if not isinstance(session, dict):
                continue
            session_id = session.get("id")
            title = session.get("title") or "(untitled)"
            directory = session.get("directory") or ""
            if isinstance(session_id, str):
                print(f"  {index}. {title}  [{directory}]  {session_id}")
        try:
            selection = input("Session number (blank cancels): ").strip()
        except (EOFError, OSError):
            return ""
        if not selection:
            return ""
        try:
            selection_index = int(selection) - 1
            if selection_index < 0:
                raise IndexError
            selected = sessions[selection_index]
        except (ValueError, IndexError):
            print("Invalid OpenCode session selection.")
            return ""
        session_id = selected.get("id") if isinstance(selected, dict) else None
        if not isinstance(session_id, str):
            print("Selected OpenCode session has no ID.")
            return ""
        self.opencode_session = session_id
        directory = selected.get("directory") if isinstance(selected, dict) else None
        if isinstance(directory, str) and directory:
            self.opencode_cwd = Path(directory)
        return session_id

    def _send_to_opencode(self, prompt: str) -> None:
        session_id = self._choose_opencode_session()
        if not session_id:
            print("OpenCode command cancelled.")
            return
        if not self._confirm(f"OpenCode session {session_id}", prompt):
            return
        command = [
            self._opencode_cli(),
            "run",
            "--session",
            session_id,
            prompt,
        ]
        print("Starting OpenCode CLI. Review and approve any tool permissions it requests.")
        try:
            result = subprocess.run(
                command,
                cwd=self.opencode_cwd or Path.cwd(),
                timeout=self.timeout,
                check=False,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise RuntimeError(f"Could not run OpenCode: {error}") from error
        if result.returncode != 0:
            raise RuntimeError(
                f"OpenCode CLI failed with exit code {result.returncode}"
            )

    def _send_to_claude(self, prompt: str) -> None:
        executable = shutil.which(self.claude_command)
        if executable is None:
            raise RuntimeError(
                f"Claude Code command {self.claude_command!r} was not found on PATH"
            )
        command = [executable, "-p", "--output-format", "text"]
        if self.claude_session:
            command.extend(("--resume", self.claude_session))
        command.append(prompt)
        cwd = self.claude_cwd
        description = (
            f"Claude Code session {self.claude_session}"
            if self.claude_session
            else f"Claude Code one-shot in {cwd or Path.cwd()}"
        )
        if not self._confirm(description, prompt):
            return
        print("Starting Claude Code...")
        try:
            result = subprocess.run(
                command,
                cwd=cwd,
                timeout=self.timeout,
                check=False,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise RuntimeError(f"Claude Code invocation failed: {error}") from error
        if result.returncode:
            print(f"Claude Code exited with status {result.returncode}.")

    def _send_to_copilot(self, prompt: str) -> None:
        executable = shutil.which(self.copilot_command)
        if executable is None:
            raise RuntimeError(
                f"GitHub Copilot CLI command {self.copilot_command!r} "
                "was not found on PATH"
            )
        cwd = self.copilot_cwd
        description = f"GitHub Copilot CLI in {cwd or Path.cwd()}"
        if not self._confirm(description, prompt):
            return
        command = [executable, "--prompt", prompt]
        print(
            "Starting GitHub Copilot CLI. Review and approve any tool "
            "permissions it requests."
        )
        try:
            result = subprocess.run(
                command,
                cwd=cwd,
                timeout=self.timeout,
                check=False,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise RuntimeError(
                f"GitHub Copilot CLI invocation failed: {error}"
            ) from error
        if result.returncode:
            print(f"GitHub Copilot CLI exited with status {result.returncode}.")

    def _run_action(self, name: str) -> None:
        action = self.actions.get(name.casefold())
        if action is None:
            available = ", ".join(sorted(self.actions)) or "(none configured)"
            print(f"Unknown action {name!r}. Available actions: {available}")
            return
        display = action.description or name
        target = f"action {name!r}: {display}\n  command: {list(action.command)!r}"
        if action.cwd is not None:
            target += f"\n  working directory: {action.cwd}"
        if not self._confirm(target, "Run this configured action?"):
            return
        executable = shutil.which(action.command[0])
        if executable is None:
            raise RuntimeError(
                f"Action executable {action.command[0]!r} was not found on PATH"
            )
        command = (executable, *action.command[1:])
        if action.cwd is not None and not action.cwd.is_dir():
            raise RuntimeError(f"Action working directory does not exist: {action.cwd}")
        print(f"Running action {name!r}...")
        try:
            result = subprocess.run(
                command,
                cwd=action.cwd,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=self.timeout,
                check=False,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise RuntimeError(f"Action {name!r} failed: {error}") from error
        if result.stdout.strip():
            print(result.stdout.strip())
        if result.stderr.strip():
            print(result.stderr.strip(), file=sys.stderr)
        if result.returncode:
            print(f"Action exited with status {result.returncode}.")
