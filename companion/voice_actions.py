"""Route explicitly prefixed final transcripts to local agent harnesses."""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import unicodedata
from dataclasses import dataclass
from difflib import SequenceMatcher
from pathlib import Path
from typing import Optional

from interactive_prompts import prompt_choice


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

_SPOKEN_ACTION = re.compile(
    r"^(?:(?:please|can you|could you|would you|will you|"
    r"i want you to|i'd like you to|s'il vous plait|"
    r"s'il te plait|svp|"
    r"peux tu|pouvez vous|pourrais tu|pourriez vous|"
    r"est ce que tu peux|est ce que vous pouvez|je veux|"
    r"je voudrais|j'aimerais)\s+)*"
    r"(?:run|start|launch|execute|do|perform|trigger|open|show|check|list|"
    r"lance|lancer|demarre|demarrer|ouvre|ouvrir|affiche|afficher|"
    r"montre|montrer|verifie|verifier|liste|fais|faire)"
    r"[\s,]+(?:(?:the|my|le|la|les|l')\s*)?"
    r"(?:(?:configured\s+)?action\s*[:,]?\s*)?"
    r"(?:me\s+)?(.+?)"
    r"(?:\s+(?:the\s+)?(?:action|command))?"
    r"(?:\s+(?:please|now|for me|s'il vous plait|maintenant))?"
    r"\s*[.!?]*\s*$"
)


def _fold_text(text: str) -> str:
    decomposed = unicodedata.normalize("NFKD", text.casefold())
    folded = "".join(
        character for character in decomposed
        if not unicodedata.combining(character)
    )
    return folded.replace("’", "'")


def _normalize_action_name(name: str) -> str:
    folded = _fold_text(name)
    # Collapse spaced-out acronyms: "O T A" -> "OTA", "C I" -> "CI"
    folded = re.sub(r"\b([a-z])\s+([a-z])\s+(?=[a-z])", r"\1\2", folded)
    folded = re.sub(r"\b([a-z])\s+([a-z])\b", r"\1\2", folded)
    return "".join(character for character in folded if character.isalnum())


def _spoken_action_request(transcript: str) -> Optional[str]:
    folded = re.sub(r"[-_]+", " ", _fold_text(transcript.strip()))
    folded = folded.replace("«", " ").replace("»", " ")
    # Collapse spaced-out acronyms: "O T A update" -> "OTA update"
    folded = re.sub(r"\b([a-z])\s+([a-z])\s+(?=[a-z])", r"\1\2", folded)
    folded = re.sub(r"\b([a-z])\s+([a-z])\b", r"\1\2", folded)
    match = _SPOKEN_ACTION.match(folded)
    return match.group(1).strip(" \t,.;:!?") if match is not None else None


def parse_intent(transcript: str) -> Intent:
    """Parse a spoken target prefix; unprefixed speech remains dictation."""
    stripped = transcript.strip()
    for prefix, target in _PREFIXES:
        if target == "opencode":
            pattern = r"^\s*open[\s_-]*code(?=$|[\s,:;.-])"
        else:
            words = re.escape(prefix).replace(r"\ ", r"[\s_-]*")
            pattern = rf"^\s*{words}(?=$|[\s,:;.-])"
        match = re.match(pattern, stripped, flags=re.IGNORECASE)
        if match is not None:
            content = stripped[match.end():].lstrip(" \t,:;.-")
            return Intent(target, content) if content else Intent("pending", "")
    return Intent("dictation", stripped)


def default_actions_path() -> Path:
    root = os.environ.get("LOCALAPPDATA") or os.environ.get("XDG_CONFIG_HOME")
    base = Path(root) if root else Path.home() / ".config"
    return base / "StickS3Intercom" / "actions.json"


def _agent_cli_options() -> dict[str, int | None]:
    if "STICKS3_INTERACTION_DIR" not in os.environ:
        return {}
    return {
        "stdin": None,
        "stdout": None,
        "stderr": None,
        "creationflags": getattr(subprocess, "CREATE_NEW_CONSOLE", 0),
    }


@dataclass(frozen=True)
class Action:
    command: tuple[str, ...]
    cwd: Optional[Path]
    description: str
    interactive: bool = False
    aliases: tuple[str, ...] = ()


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
    seen_phrases: dict[str, str] = {}
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
        interactive = definition.get("interactive", False)
        if not isinstance(interactive, bool):
            raise ValueError(f"action {name!r} has a non-boolean interactive flag")
        aliases = definition.get("aliases", [])
        if (not isinstance(aliases, list) or
                any(not isinstance(alias, str) or not alias.strip()
                    for alias in aliases)):
            raise ValueError(f"action {name!r} has invalid aliases")
        for phrase in (name, *(alias.strip() for alias in aliases)):
            normalized = _normalize_action_name(phrase)
            if not normalized:
                raise ValueError(f"action {name!r} has an unusable name or alias")
            other_name = seen_phrases.get(normalized)
            if other_name is not None:
                raise ValueError(
                    f"action name or alias {phrase!r} conflicts with "
                    f"action {other_name!r}"
                )
            seen_phrases[normalized] = name
        actions[name.casefold()] = Action(
            tuple(command), cwd, description, interactive,
            tuple(alias.strip() for alias in aliases),
        )
    return actions


class VoiceCommandRouter:
    def __init__(
        self,
        *,
        opencode_command: str,
        opencode_session: Optional[str],
        opencode_cwd: Optional[Path] = None,
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
        self.opencode_cwd = opencode_cwd
        self.claude_command = claude_command
        self.claude_session = claude_session
        self.claude_cwd = claude_cwd
        self.copilot_command = copilot_command
        self.copilot_cwd = copilot_cwd
        self.timeout = timeout
        self.actions_path = actions_path
        self.actions = load_actions(actions_path)
        self._interactive_lock = threading.Lock()
        self._interactive_process: subprocess.Popen[bytes] | None = None
        self._interactive_session: Path | None = None
        self._interactive_pending = False

    def route(self, transcript: str) -> Optional[str]:
        """Route final speech without blocking transcription on interactive actions."""
        if self._forward_to_active_prompt(transcript):
            return None

        intent = parse_intent(transcript)
        action_request = (
            intent.content if intent.target == "action"
            else _spoken_action_request(transcript)
            if intent.target == "dictation" else None
        )
        if action_request is not None:
            try:
                self.actions = load_actions(self.actions_path)
            except ValueError as error:
                print(f"Could not reload voice actions: {error}", file=sys.stderr)
                return None
            matched_name = self._match_action_name(action_request)
            if matched_name is not None:
                intent = Intent("action", matched_name)
            elif intent.target == "action":
                intent = Intent("action", action_request)
        if intent.target == "action" and intent.content:
            action = self.actions.get(intent.content.casefold())
            if action is not None and action.interactive:
                self._start_confirmed_interactive_action(intent.content)
                return None
        return self._handle_intent(intent)

    def handle(self, transcript: str) -> Optional[str]:
        """Return dictation text, or route a prefixed command and return None."""
        if self._forward_to_active_prompt(transcript):
            return None
        intent = parse_intent(transcript)
        action_request = (
            intent.content if intent.target == "action"
            else _spoken_action_request(transcript)
            if intent.target == "dictation" else None
        )
        if action_request is not None:
            matched_name = self._match_action_name(action_request)
            if matched_name is not None:
                intent = Intent("action", matched_name)
            elif intent.target == "action":
                intent = Intent("action", action_request)
        return self._handle_intent(intent)

    def _handle_intent(self, intent: Intent) -> Optional[str]:
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

    def _match_action_name(self, spoken_name: str) -> Optional[str]:
        normalized = _normalize_action_name(spoken_name)
        if not normalized:
            return None
        candidates_by_action = {
            name: (name, *action.aliases)
            for name, action in self.actions.items()
        }
        for name, phrases in candidates_by_action.items():
            if any(_normalize_action_name(phrase) == normalized
                   for phrase in phrases):
                return name

        if len(normalized) < 6:
            return None
        candidates = [
            (
                SequenceMatcher(
                    None, normalized, _normalize_action_name(phrase)
                ).ratio(),
                name,
            )
            for name, phrases in candidates_by_action.items()
            for phrase in phrases
            if len(_normalize_action_name(phrase)) >= 6
            and len(normalized) <= len(_normalize_action_name(phrase)) * 1.25
        ]
        candidates.sort(reverse=True)
        best_by_action: dict[str, float] = {}
        for score, name in candidates:
            best_by_action[name] = max(score, best_by_action.get(name, 0.0))
        candidates = sorted(
            ((score, name) for name, score in best_by_action.items()),
            reverse=True,
        )
        if not candidates or candidates[0][0] < 0.84:
            return None
        if len(candidates) > 1 and candidates[0][0] - candidates[1][0] < 0.08:
            return None
        return candidates[0][1]

    def _forward_to_active_prompt(self, transcript: str) -> bool:
        with self._interactive_lock:
            process = self._interactive_process
            session = self._interactive_session
            pending = self._interactive_pending
            active = pending or (
                process is not None and process.poll() is None
            )
            if not active or session is None:
                return False

            prompt_path = session / "active_prompt.json"
            try:
                prompt = json.loads(prompt_path.read_text(encoding="utf-8"))
            except FileNotFoundError:
                return active
            except (OSError, json.JSONDecodeError) as error:
                print(f"Could not read the active companion prompt: {error}",
                      file=sys.stderr)
                return True

            if prompt.get("kind") == "choice":
                try:
                    with (session / "voice_inbox.jsonl").open(
                        "a", encoding="utf-8"
                    ) as voice_inbox:
                        voice_inbox.write(
                            json.dumps({"text": transcript}) + "\n"
                        )
                except OSError as error:
                    print(
                        f"Could not forward speech to the active popup: {error}",
                        file=sys.stderr,
                    )
            return True

    def has_active_prompt(self) -> bool:
        with self._interactive_lock:
            process = self._interactive_process
            session = self._interactive_session
            return bool(
                session is not None
                and (
                    self._interactive_pending
                    or (
                        process is not None
                        and process.poll() is None
                    )
                )
            )

    @staticmethod
    def _confirm(
        target: str,
        content: str,
        *,
        session_dir: Path | None = None,
    ) -> bool:
        if session_dir is None:
            interaction_dir = os.environ.get("STICKS3_INTERACTION_DIR")
            if interaction_dir:
                session_dir = Path(interaction_dir)
        if session_dir is not None:
            selection = prompt_choice(
                "Confirm voice action",
                f"{target}\n\n{content}",
                ("No, cancel", "Yes, run action"),
                voice_aliases=("no", "yes"),
                session_dir=session_dir,
            )
            return selection == 1

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
                cwd=self.opencode_cwd or Path.cwd(),
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

        valid_sessions = [
            session for session in sessions
            if isinstance(session, dict) and isinstance(session.get("id"), str)
        ]

        print("\nOpenCode sessions:")
        if valid_sessions:
            for index, session in enumerate(valid_sessions, start=1):
                session_id = session.get("id")
                title = session.get("title") or "(untitled)"
                directory = session.get("directory") or ""
                print(f"  {index}. {title}  [{directory}]  {session_id}")
        else:
            print("  (no existing sessions)")

        # Add "Create new session" as an option
        new_session_option = len(valid_sessions) + 1
        print(f"  {new_session_option}. Create new session")

        interaction_dir = os.environ.get("STICKS3_INTERACTION_DIR")
        if interaction_dir:
            choices = tuple(
                f"{session.get('title') or '(untitled)'} — "
                f"{session.get('directory') or ''}"
                for session in valid_sessions
            ) + ("Create new session",)
            aliases = tuple(
                str(session.get("title") or f"session {index + 1}")
                for index, session in enumerate(valid_sessions)
            ) + ("new session",)
            selected_index = prompt_choice(
                "Choose OpenCode session",
                "Select the session that should receive this voice command, or create a new one.",
                choices,
                voice_aliases=aliases,
                session_dir=Path(interaction_dir),
            )
            if selected_index is None:
                return ""
            if selected_index == new_session_option - 1:
                return "__new__"
            selected = valid_sessions[selected_index]
        else:
            try:
                selection = input(
                    f"Session number (1-{new_session_option}, blank cancels): "
                ).strip()
            except (EOFError, OSError):
                return ""
            if not selection:
                return ""
            try:
                selection_index = int(selection) - 1
                if selection_index < 0:
                    raise IndexError
                if selection_index == new_session_option - 1:
                    return "__new__"
                selected = valid_sessions[selection_index]
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
        if session_id == "__new__":
            session_display = "new session"
            if not self._confirm(f"OpenCode {session_display}", prompt):
                return
            command = [
                self._opencode_cli(),
                "run",
                prompt,
            ]
        else:
            session_display = f"session {session_id}"
            if not self._confirm(f"OpenCode {session_display}", prompt):
                return
            command = [
                self._opencode_cli(),
                "run",
                "--session",
                session_id,
                prompt,
            ]
        print(f"Starting OpenCode CLI ({session_display}). Review and approve any tool permissions it requests.")
        try:
            options = _agent_cli_options()
            options["stderr"] = subprocess.PIPE
            options["text"] = True
            result = subprocess.run(
                command,
                cwd=self.opencode_cwd or Path.cwd(),
                timeout=self.timeout,
                check=False,
                **options,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise RuntimeError(f"Could not run OpenCode: {error}") from error
        if result.returncode != 0:
            detail = "\n".join(
                output.strip()
                for output in (result.stdout, result.stderr)
                if output and output.strip()
            )
            suffix = f": {detail}" if detail else ""
            raise RuntimeError(
                f"OpenCode CLI failed with exit code {result.returncode}{suffix}"
            )
        # If we created a new session, capture its ID for future use
        if session_id == "__new__" and result.stdout:
            try:
                # opencode run outputs the session ID when creating a new session
                output_data = json.loads(result.stdout.strip())
                if isinstance(output_data, dict) and isinstance(output_data.get("id"), str):
                    self.opencode_session = output_data["id"]
                    directory = output_data.get("directory")
                    if isinstance(directory, str) and directory:
                        self.opencode_cwd = Path(directory)
            except (json.JSONDecodeError, AttributeError):
                pass

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
                **_agent_cli_options(),
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
                **_agent_cli_options(),
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise RuntimeError(
                f"GitHub Copilot CLI invocation failed: {error}"
            ) from error
        if result.returncode:
            print(f"GitHub Copilot CLI exited with status {result.returncode}.")

    def _run_action(
        self,
        name: str,
        *,
        session_dir: Path | None = None,
    ) -> None:
        matched_name = self._match_action_name(name)
        action = self.actions.get(matched_name) if matched_name else None
        if action is None:
            available = ", ".join(sorted(self.actions)) or "(none configured)"
            print(f"Unknown action {name!r}. Available actions: {available}")
            return
        display = action.description or name
        target = f"action {name!r}: {display}\n  command: {list(action.command)!r}"
        if action.cwd is not None:
            target += f"\n  working directory: {action.cwd}"
        if not self._confirm(
            target,
            "Run this configured action?",
            session_dir=session_dir,
        ):
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
            if action.interactive:
                self._start_interactive_action(
                    command,
                    cwd=action.cwd,
                    name=name,
                    session=session_dir,
                )
                return
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

    def _start_interactive_action(
        self,
        command: tuple[str, ...],
        *,
        cwd: Optional[Path],
        name: str,
        session: Path | None = None,
    ) -> None:
        owns_session = session is None
        with self._interactive_lock:
            if (
                self._interactive_pending
                or (
                    self._interactive_process is not None
                    and self._interactive_process.poll() is None
                )
            ):
                if session is None or session != self._interactive_session:
                    raise RuntimeError("Another interactive action is still running.")

            if session is None:
                session = Path(tempfile.mkdtemp(prefix="sticks3-interaction-"))
            environment = os.environ.copy()
            environment["STICKS3_INTERACTION_DIR"] = str(session)
            try:
                process = subprocess.Popen(
                    command,
                    cwd=cwd,
                    env=environment,
                    creationflags=getattr(subprocess, "CREATE_NEW_CONSOLE", 0),
                )
            except OSError:
                if owns_session:
                    shutil.rmtree(session)
                raise
            self._interactive_process = process
            self._interactive_session = session
            self._interactive_pending = False

        threading.Thread(
            target=self._wait_for_interactive_action,
            args=(process, session, name),
            name="interactive-action",
            daemon=True,
        ).start()

    def _start_confirmed_interactive_action(self, name: str) -> None:
        session = Path(tempfile.mkdtemp(prefix="sticks3-interaction-"))
        with self._interactive_lock:
            if (
                self._interactive_pending
                or (
                    self._interactive_process is not None
                    and self._interactive_process.poll() is None
                )
            ):
                shutil.rmtree(session)
                raise RuntimeError("Another interactive action is still running.")
            self._interactive_session = session
            self._interactive_process = None
            self._interactive_pending = True

        worker = threading.Thread(
            target=self._run_confirmed_interactive_action,
            args=(name, session),
            name="interactive-action-launch",
            daemon=True,
        )
        try:
            worker.start()
        except RuntimeError:
            with self._interactive_lock:
                self._interactive_session = None
                self._interactive_pending = False
            shutil.rmtree(session)
            raise

    def _run_confirmed_interactive_action(
        self,
        name: str,
        session: Path,
    ) -> None:
        try:
            self._run_action(name, session_dir=session)
        except (OSError, RuntimeError, ValueError) as error:
            print(f"Voice action {name!r} failed: {error}", file=sys.stderr)
        finally:
            with self._interactive_lock:
                cleanup = (
                    self._interactive_session == session
                    and self._interactive_pending
                )
                if cleanup:
                    self._interactive_session = None
                    self._interactive_process = None
                    self._interactive_pending = False
            if cleanup:
                try:
                    shutil.rmtree(session)
                except OSError as error:
                    print(f"Could not clean up action session {session}: {error}",
                          file=sys.stderr)

    def _wait_for_interactive_action(
        self,
        process: subprocess.Popen[bytes],
        session: Path,
        name: str,
    ) -> None:
        try:
            return_code = process.wait(timeout=self.timeout)
        except subprocess.TimeoutExpired:
            print(f"Interactive action {name!r} timed out; stopping it.",
                  file=sys.stderr)
            process.terminate()
            try:
                return_code = process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                return_code = process.wait()
        print(f"Interactive action {name!r} exited with status {return_code}.")
        with self._interactive_lock:
            if self._interactive_process is process:
                self._interactive_process = None
                self._interactive_session = None
                self._interactive_pending = False
        try:
            shutil.rmtree(session)
        except OSError as error:
            print(f"Could not clean up action session {session}: {error}",
                  file=sys.stderr)
